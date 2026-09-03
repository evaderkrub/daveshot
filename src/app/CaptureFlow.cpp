#include "app/CaptureFlow.h"

#include "app/CaptureService.h"

namespace daveshot
{
namespace
{
    // A capture that started from the tray goes back to the tray: the
    // person pressed a key to get a picture, not to get a window, and the
    // picture has been copied or saved. The one exception is a shot that
    // neither happens to, because then the window is the only place it
    // exists. (A capture that failed is reported through the error modal,
    // which brings the window back on its own -- see the loop.)
    bool StaysInTray(const AppState& state, bool tookShot)
    {
        if (!state.inTray)
            return false;
        return !tookShot || state.settings.autoCopy || state.settings.autoSave;
    }

    // `tookShot` says whether a new capture went into the history on the
    // way here, which is what decides whether the window is needed.
    void Finish(AppState& state, CaptureEffects& effects, bool tookShot)
    {
        const bool stayAway = StaysInTray(state, tookShot);

        // Hidden before the overlay comes down, not after: leaving the
        // overlay puts the ordinary window's frame and size back, and doing
        // that to a window that is on screen shows it for a frame.
        if (stayAway)
            effects.HideWindow();

        if (state.selecting || !state.backdrop.pixels.empty())
        {
            effects.LeaveOverlay();
            state.backdrop.Reset();
            state.selecting = false;
            state.selection = Rect{};
        }

        if (!stayAway)
        {
            effects.ShowWindow();
            state.inTray = false;
        }
        state.phase = CapturePhase::Idle;
    }

    // Takes the shot for a non-region mode and runs it through save/copy.
    void ShootDirect(AppState& state, CaptureEffects& effects)
    {
        Image       image;
        Rect        source;
        std::string error;

        if (!effects.Grab(state.request, image, source, error))
        {
            ReportError(state, error);
            state.status = "Capture failed";
            Finish(state, effects, false);
            return;
        }

        Shot shot = MakeShot(std::move(image), state.request.mode, source, LocalTimeNow());
        Shot& stored = AcceptShot(state, std::move(shot));
        ApplyPostCapture(state, stored);
        Finish(state, effects, true);
    }

    void BeginRegion(AppState& state, CaptureEffects& effects)
    {
        std::string error;
        if (!effects.GrabDesktop(state.backdrop, state.backdropBounds, error))
        {
            ReportError(state, error);
            state.status = "Capture failed";
            Finish(state, effects, false);
            return;
        }

        Rect covered;
        if (!effects.EnterOverlay(state.backdropBounds, covered, error))
        {
            ReportError(state, error);
            Finish(state, effects, false);
            return;
        }

        // An overlay that covers less than the whole desktop shows only its
        // own part of the frozen picture, so the selection the user draws on
        // it and the pixels underneath stay in the same space.
        if (!covered.Empty() && covered != state.backdropBounds)
        {
            const Rect visible = Intersect(covered, state.backdropBounds);
            Rect local = visible;
            local.x -= state.backdropBounds.x;
            local.y -= state.backdropBounds.y;

            Image part;
            if (visible.Empty() || !CropImage(state.backdrop, local, part))
            {
                ReportError(state, "the overlay is not on the captured desktop");
                Finish(state, effects, false);
                return;
            }
            state.backdrop       = std::move(part);
            state.backdropBounds = visible;
        }

        state.selecting = false;
        state.selection = Rect{};
        state.phase     = CapturePhase::Selecting;
        state.status    = "Drag to select an area  --  Esc to cancel";
    }
}

void RequestCapture(AppState& state, const CaptureRequest& request, double now)
{
    if (CaptureInProgress(state))
        return;

    state.request = request;

    const int delay = ClampDelaySeconds(request.delaySeconds);
    if (delay > 0)
    {
        state.phase        = CapturePhase::Countdown;
        state.countdownEnd = now + (double)delay;
        state.phaseUntil   = state.countdownEnd;
        state.status       = "Capturing in a moment...";
        return;
    }

    state.phase      = CapturePhase::Hiding;
    state.phaseUntil = now + (state.settings.hideOnCapture ? kHideSettleSeconds : 0.0);
    state.status     = "Capturing...";
}

void TickCapture(AppState& state, CaptureEffects& effects, double now)
{
    switch (state.phase)
    {
    case CapturePhase::Idle:
        return;

    case CapturePhase::Countdown:
        if (now >= state.countdownEnd)
        {
            state.phase      = CapturePhase::Hiding;
            state.phaseUntil = now + (state.settings.hideOnCapture ? kHideSettleSeconds : 0.0);
            state.status     = "Capturing...";
        }
        return;

    case CapturePhase::Hiding:
    {
        // The last moment the window is still up, and so the only moment the
        // desktop will let us ask whether we may capture at all.
        if (effects.NeedsCapturePermission())
        {
            std::string error;
            if (!effects.RequestCapturePermission(error))
            {
                ReportError(state, error);
                state.status = "Capture failed";
                Finish(state, effects, false);
                return;
            }

            // Asking blocks for as long as the user takes to answer, which
            // leaves `now` stale by however long that was -- hiding against
            // it would shoot before the window is off the screen. Going back
            // through a countdown that has already expired restarts the hide
            // on the next frame's clock, which is a fresh one.
            state.phase        = CapturePhase::Countdown;
            state.countdownEnd = now;
            state.phaseUntil   = now;
            return;
        }

        // Hiding is requested every frame of this phase rather than once on
        // entry: it is idempotent, and it saves carrying a "have we hidden
        // yet" flag through a phase that exists only to wait.
        if (state.settings.hideOnCapture)
            effects.HideWindow();

        if (now < state.phaseUntil)
            return;

        if (state.request.mode == CaptureMode::Region)
            BeginRegion(state, effects);
        else
            ShootDirect(state, effects);
        return;
    }

    case CapturePhase::Selecting:
        // Driven by the overlay; CommitSelection or CancelCapture ends it.
        return;

    case CapturePhase::Finishing:
        Finish(state, effects, false);
        return;
    }
}

void CancelCapture(AppState& state, CaptureEffects& effects)
{
    if (!CaptureInProgress(state))
        return;
    state.status = "Capture cancelled";
    Finish(state, effects, false);
}

void CommitSelection(AppState& state, CaptureEffects& effects)
{
    if (state.phase != CapturePhase::Selecting)
        return;

    const Rect area = state.selection;
    if (area.w < kMinSelection || area.h < kMinSelection)
    {
        // Too small to be a deliberate drag. Stay in the overlay and let the
        // user try again rather than handing them a 2x1 pixel capture.
        state.selecting = false;
        state.selection = Rect{};
        return;
    }

    Image cropped;
    if (!CropImage(state.backdrop, area, cropped))
    {
        ReportError(state, "that selection is outside the captured desktop");
        Finish(state, effects, false);
        return;
    }

    // The selection is in backdrop image space; report it in screen
    // coordinates, which is what the user would recognise.
    Rect source = area;
    source.x += state.backdropBounds.x;
    source.y += state.backdropBounds.y;

    Shot shot = MakeShot(std::move(cropped), CaptureMode::Region, source, LocalTimeNow());
    Shot& stored = AcceptShot(state, std::move(shot));
    ApplyPostCapture(state, stored);
    Finish(state, effects, true);
}

void ApplyPostCapture(AppState& state, Shot& shot)
{
    std::string status = shot.label;
    std::string failure;

    if (state.settings.autoCopy)
    {
        std::string error;
        if (capture::Copy(shot, error))
            status += "  -  copied";
        else
            failure = error;
    }

    if (state.settings.autoSave)
    {
        std::string error;
        if (capture::Save(shot, state.settings, error))
        {
            status += "  -  saved";
        }
        else if (failure.empty())
        {
            failure = error;
        }
    }

    state.status = status;

    // A capture that could not be saved still exists in the history, so the
    // user can retry the save by hand. Say what went wrong; do not throw the
    // picture away.
    if (!failure.empty())
        ReportError(state, failure);
}
}
