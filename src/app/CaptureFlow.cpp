#include "app/CaptureFlow.h"

#include "app/CaptureService.h"

namespace daveshot
{
namespace
{
    void Finish(AppState& state, CaptureEffects& effects)
    {
        if (state.selecting || !state.backdrop.pixels.empty())
        {
            effects.LeaveOverlay();
            state.backdrop.Reset();
            state.selecting = false;
            state.selection = Rect{};
        }
        effects.ShowWindow();
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
            Finish(state, effects);
            return;
        }

        Shot shot = MakeShot(std::move(image), state.request.mode, source, LocalTimeNow());
        Shot& stored = AcceptShot(state, std::move(shot));
        ApplyPostCapture(state, stored);
        Finish(state, effects);
    }

    void BeginRegion(AppState& state, CaptureEffects& effects)
    {
        std::string error;
        if (!effects.GrabDesktop(state.backdrop, state.backdropBounds, error))
        {
            ReportError(state, error);
            state.status = "Capture failed";
            Finish(state, effects);
            return;
        }

        if (!effects.EnterOverlay(state.backdropBounds, error))
        {
            ReportError(state, error);
            Finish(state, effects);
            return;
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
        Finish(state, effects);
        return;
    }
}

void CancelCapture(AppState& state, CaptureEffects& effects)
{
    if (!CaptureInProgress(state))
        return;
    state.status = "Capture cancelled";
    Finish(state, effects);
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
        Finish(state, effects);
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
    Finish(state, effects);
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
