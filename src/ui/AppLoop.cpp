#include "ui/AppLoop.h"

#include "app/AppState.h"
#include "app/CaptureFlow.h"
#include "app/CaptureService.h"
#include "platform/Clipboard.h"
#include "platform/Host.h"
#include "platform/Hotkeys.h"
#include "platform/Paths.h"
#include "platform/Screen.h"
#include "ui/Fonts.h"
#include "ui/Textures.h"
#include "ui/Theme.h"
#include "ui/UiRoot.h"

#include <SDL3/SDL.h>

#include "imgui.h"

namespace daveshot::ui
{
namespace
{
    constexpr float kBaseFontSize = 17.0f;

    // Nothing has a window yet when startup fails, so the message goes to the
    // OS rather than to the interface.
    void ShowStartupFailure(const std::string& message)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                                 "daveshot could not start",
                                 message.c_str(),
                                 nullptr);
    }

    // The capture sequence's view of the window and the screen. Everything
    // here is a one-line forward; the ordering rules live in app/CaptureFlow,
    // which is where they can be tested.
    class HostEffects final : public CaptureEffects
    {
    public:
        HostEffects(Host& host, TextureCache& textures)
            : m_host(host), m_textures(textures) {}

        void HideWindow() override { m_host.Hide(); }
        void ShowWindow() override { m_host.Show(); }

        bool NeedsCapturePermission() override { return screen::NeedsCapturePermission(); }

        bool RequestCapturePermission(std::string& error) override
        {
            // The desktop wants to see the window it is asking on behalf of,
            // and a capture can start from a global hotkey with that window
            // hidden. Put it back up first; this only ever runs once.
            m_host.Show();
            return screen::RequestCapturePermission(error);
        }

        bool EnterOverlay(const Rect& desktop, Rect& covered, std::string& error) override
        {
            return m_host.EnterOverlay(desktop, covered, error);
        }

        void LeaveOverlay() override
        {
            m_host.LeaveOverlay();
            // The frozen desktop is the single largest texture the process
            // ever holds -- a full multi-monitor RGBA image. Release it the
            // moment the overlay is done rather than at the next sweep.
            m_textures.Forget(kBackdropShotId);
        }

        bool Grab(const CaptureRequest& request, Image& out, Rect& source,
                  std::string& error) override
        {
            return capture::Grab(request, out, source, error);
        }

        bool GrabDesktop(Image& out, Rect& bounds, std::string& error) override
        {
            return capture::GrabDesktop(out, bounds, error);
        }

    private:
        Host&         m_host;
        TextureCache& m_textures;
    };

    void RefreshTargets(AppState& state)
    {
        std::string error;
        if (!screen::EnumerateMonitors(state.monitors, error))
            state.monitors.clear();

        if (state.windowListStale)
        {
            if (!screen::EnumerateWindows(state.windows, error))
                state.windows.clear();
            state.windowListStale = false;
        }
    }

    void ApplyHotkeys(AppState& state)
    {
        if (!HotkeysNeedRegistering(state))
            return;
        MarkHotkeysApplied(state);

        if (!state.settings.hotkeyEnabled)
        {
            hotkeys::UnregisterAll();
            return;
        }

        std::string error;
        if (!hotkeys::Register(state.settings.hotkeyRegion, state.settings.hotkeyScreen, error))
        {
            // A hotkey another application already owns is a normal thing to
            // hit, not a reason to stop: say which one and carry on with
            // whichever ones did register.
            ReportError(state, error);
        }
    }

    void HandleHotkeys(AppState& state)
    {
        std::vector<hotkeys::Action> fired;
        hotkeys::Drain(fired);

        const std::string late = hotkeys::TakeError();
        if (!late.empty())
            ReportError(state, late);

        for (hotkeys::Action action : fired)
        {
            if (CaptureInProgress(state))
                continue;
            state.request.mode = (action == hotkeys::Action_Region)
                               ? CaptureMode::Region
                               : CaptureMode::FullScreen;
            // A hotkey capture is aimed at whatever is on screen right now,
            // so the window list has to be current for the overlay's
            // click-a-window shortcut.
            state.windowListStale = true;
            state.pendingRequest  = true;
        }
    }

    // Acts on everything the interface asked for this frame. Kept in one
    // place so the panels can stay pure: they raise a flag, this clears it.
    void ApplyIntents(AppState& state, HostEffects& effects, double now)
    {
        if (state.pendingRequest)
        {
            state.pendingRequest = false;
            if (state.request.mode == CaptureMode::Region ||
                state.request.mode == CaptureMode::Window)
                RefreshTargets(state);
            RequestCapture(state, state.request, now);
        }

        if (state.cancelRequested)
        {
            state.cancelRequested = false;
            CancelCapture(state, effects);
        }

        if (state.pendingSelection)
        {
            state.pendingSelection = false;
            CommitSelection(state, effects);
        }

        if (state.saveRequested)
        {
            state.saveRequested = false;
            if (Shot* shot = CurrentShot(state))
            {
                std::string error;
                if (capture::Save(*shot, state.settings, error))
                    state.status = "Saved " + shot->savedPath;
                else
                    ReportError(state, error);
            }
        }

        if (state.copyRequested)
        {
            state.copyRequested = false;
            if (const Shot* shot = CurrentShot(state))
            {
                std::string error;
                if (capture::Copy(*shot, error))
                    state.status = "Copied to the clipboard";
                else
                    ReportError(state, error);
            }
        }

        if (state.copyPathRequested)
        {
            state.copyPathRequested = false;
            if (const Shot* shot = CurrentShot(state))
            {
                std::string error;
                if (shot->savedPath.empty())
                    ReportError(state, "that capture has not been saved yet");
                else if (clipboard::CopyText(shot->savedPath, error))
                    state.status = "Copied the path";
                else
                    ReportError(state, error);
            }
        }

        if (state.revealRequested)
        {
            state.revealRequested = false;
            if (const Shot* shot = CurrentShot(state))
            {
                if (!shot->savedPath.empty() && !paths::RevealInFileBrowser(shot->savedPath))
                    ReportError(state, "could not open " + shot->savedPath);
            }
        }
    }
}

int RunApplication(int argc, char** argv)
{
    std::string error;

    if (!paths::Init(argc > 0 ? argv[0] : nullptr, error))
    {
        ShowStartupFailure(error);
        return 1;
    }

    AppState state;
    state.settingsPath = paths::Beside("daveshot_settings.txt");

    // A settings file we cannot read is worth telling the user about, but it
    // is not a reason to refuse to start: defaults are perfectly usable.
    std::string settingsError;
    if (!LoadSettings(state.settingsPath, state.settings, settingsError))
        ReportError(state, settingsError);
    state.history.limit = ClampHistoryLimit(state.settings.historyLimit);

    // Checked before the window exists, because Host::Startup points ImGui at
    // this file and ImGui writes it back out on exit -- after that it always
    // exists, and "is this a first run" can no longer be asked.
    state.layoutPending = !paths::Exists(paths::Beside("daveshot_layout.ini"));

    Host host;
    if (!host.Startup("daveshot", 1280, 800, error))
    {
        ShowStartupFailure(error);
        return 1;
    }

    std::string fontError;
    if (!fonts::Load(kBaseFontSize, fontError) && state.error.empty())
        ReportError(state, fontError);

    TextureCache textures;
    textures.SetHost(&host);
    HostEffects effects(host, textures);

    hotkeys::Install();
    RefreshTargets(state);

    while (host.PumpEvents() && !state.quitRequested)
    {
        const double now = host.Now();

        ApplyHotkeys(state);
        HandleHotkeys(state);
        ApplyIntents(state, effects, now);
        TickCapture(state, effects, now);

        // Style and font scale are rebuilt between frames only. ScaleAllSizes
        // is not idempotent, and changing metrics mid-frame leaves the rest of
        // that frame measured against the old ones.
        if (StyleNeedsRebuild(state))
        {
            // The user's scale multiplies the display's own, so 125% in
            // Windows and 1.25x here compound the way the user expects.
            theme::Apply(theme::IndexByName(state.settings.theme.c_str()),
                         state.settings.uiScale * host.DisplayScale());
            MarkStyleApplied(state);
        }

        textures.Sweep(state.history);

        host.BeginFrame();
        Draw(state, textures);

        // The overlay paints the frozen desktop over every pixel, so a
        // coloured clear behind it would only ever be seen as a flash while
        // the window resizes onto the desktop.
        const ImVec4 bg = host.InOverlay() ? ImVec4(0, 0, 0, 1)
                                           : ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        host.EndFrame(bg.x, bg.y, bg.z);
    }

    std::string saveError;
    if (!SaveSettings(state.settingsPath, state.settings, saveError))
    {
        // Nothing left to draw the message into; the window is on its way out.
        SDL_Log("daveshot: %s", saveError.c_str());
    }

    // Textures have to go before the renderer that owns them.
    textures.Clear();
    hotkeys::Shutdown();
    host.Shutdown();
    return 0;
}
}
