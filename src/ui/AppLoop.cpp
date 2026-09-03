#include "ui/AppLoop.h"

#include "app/AppState.h"
#include "app/CaptureFlow.h"
#include "app/CaptureService.h"
#include "platform/Autostart.h"
#include "platform/Host.h"
#include "platform/Hotkeys.h"
#include "platform/PrintScreenKey.h"
#include "platform/Paths.h"
#include "platform/Screen.h"
#include "platform/SingleInstance.h"
#include "platform/Tray.h"
#include "ui/Fonts.h"
#include "ui/Textures.h"
#include "ui/Theme.h"
#include "ui/UiRoot.h"

#include <SDL3/SDL.h>

#include "imgui.h"

#include <cstring>

namespace daveshot::ui
{
namespace
{
    constexpr float kBaseFontSize = 17.0f;

    // How long the loop sleeps between looks while the window is in the
    // tray. Short enough that a hotkey the OS cannot wake us for -- an X11
    // grab arrives on a connection SDL is not watching -- still feels
    // immediate; long enough that an idle daveshot costs nothing.
    constexpr int kDormantWaitMs = 50;

    // "--background": start in the tray, with no window. What the login
    // entry passes, so that signing in does not open a window over
    // whatever the person sat down to do.
    bool WantsBackgroundStart(int argc, char** argv)
    {
        for (int i = 1; i < argc; ++i)
            if (argv[i] != nullptr && std::strcmp(argv[i], "--background") == 0)
                return true;
        return false;
    }

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

    // Only a bare Print collides with the desktop's own screenshot tool, so
    // only a bare Print is worth asking the OS about -- the query reads the
    // registry on Windows and runs gsettings on Linux.
    bool WantsPrintScreen(const AppState& state)
    {
        return state.settings.hotkeyEnabled &&
               (hotkeys::IsBarePrintScreen(state.settings.hotkeyRegion) ||
                hotkeys::IsBarePrintScreen(state.settings.hotkeyScreen));
    }

    void RefreshPrintKey(AppState& state)
    {
        if (!state.printKeyStale)
            return;
        state.printKeyStale = false;
        state.printKey = WantsPrintScreen(state) ? printkey::Query()
                                                 : printkey::Status{};
    }

    // A capture asked for from outside the window -- a hotkey, the tray
    // menu -- is aimed at whatever is on screen right now, so the window
    // list has to be current for the overlay's click-a-window shortcut.
    void RequestExternalCapture(AppState& state, CaptureMode mode)
    {
        if (CaptureInProgress(state))
            return;
        state.request.mode    = mode;
        state.windowListStale = true;
        state.pendingRequest  = true;
    }

    void HandleHotkeys(AppState& state)
    {
        std::vector<hotkeys::Action> fired;
        hotkeys::Drain(fired);

        const std::string late = hotkeys::TakeError();
        if (!late.empty())
            ReportError(state, late);

        for (hotkeys::Action action : fired)
            RequestExternalCapture(state, (action == hotkeys::Action_Region)
                                              ? CaptureMode::Region
                                              : CaptureMode::FullScreen);
    }

    void HandleTray(AppState& state)
    {
        std::vector<tray::Action> clicked;
        tray::Drain(clicked);

        for (tray::Action action : clicked)
        {
            switch (action)
            {
            case tray::Action::Open:   state.openRequested = true; break;
            case tray::Action::Region: RequestExternalCapture(state, CaptureMode::Region); break;
            case tray::Action::Screen: RequestExternalCapture(state, CaptureMode::FullScreen); break;
            case tray::Action::Quit:   state.quitRequested = true; break;
            }
        }
    }

    // The close button means "put it away" when there is a tray to put it
    // in and the setting says so, and "quit" otherwise -- which is what it
    // meant before there was a tray.
    void HandleCloseButton(AppState& state)
    {
        if (state.trayAvailable && state.settings.closeToTray)
            state.putAwayRequested = true;
        else
            state.quitRequested = true;
    }

    void RefreshAutostart(AppState& state)
    {
        if (!state.autostartStale)
            return;
        state.autostartStale = false;
        state.autostart      = autostart::Query();
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
                if (capture::CopyPath(*shot, error))
                    state.status = "Copied the path";
                else
                    ReportError(state, error);
            }
        }

        if (state.takePrintKeyRequested || state.givePrintKeyRequested)
        {
            const bool take = state.takePrintKeyRequested;
            state.takePrintKeyRequested = false;
            state.givePrintKeyRequested = false;

            std::string keyError;
            if (take ? printkey::Take(keyError) : printkey::GiveBack(keyError))
            {
                state.status = take ? "Print Screen is daveshot's"
                                    : "Print Screen handed back to the desktop";
                // A hotkey the desktop refused while it held the key has to
                // be asked for again now that it does not.
                RefreshHotkeys(state);
            }
            else
            {
                ReportError(state, keyError);
            }
            state.printKeyStale = true;
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

        if (state.openRequested)
        {
            state.openRequested = false;
            effects.ShowWindow();
            state.inTray = false;
        }

        if (state.putAwayRequested)
        {
            state.putAwayRequested = false;
            // Mid-capture the window is already where the sequence wants
            // it, and the sequence puts it back; a request now would fight
            // that. The close button during a countdown is rare enough to
            // ignore rather than queue.
            if (!CaptureInProgress(state) && state.trayAvailable)
            {
                effects.HideWindow();
                state.inTray = true;

                // Settings are otherwise written on the way out, and an
                // application that lives in the tray may not get a way out
                // -- sign-out gives it no time. The user has just "closed"
                // it, and expects what they changed to be kept.
                std::string saveError;
                if (!state.settingsPath.empty() &&
                    !SaveSettings(state.settingsPath, state.settings, saveError))
                    ReportError(state, saveError);
            }
        }

        if (state.autostartChangeRequested)
        {
            state.autostartChangeRequested = false;
            std::string autostartError;
            const bool ok = state.autostartWanted ? autostart::Enable(autostartError)
                                                  : autostart::Disable(autostartError);
            if (ok)
                state.status = state.autostartWanted
                             ? "daveshot will start when you sign in"
                             : "daveshot will not start on its own";
            else
                ReportError(state, autostartError);
            state.autostartStale = true;
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

    // A second launch -- the Start menu while one daveshot is already in
    // the tray -- has asked the first one to come forward, and has nothing
    // left to do.
    if (!instance::Claim())
        return 0;

    const bool background = WantsBackgroundStart(argc, argv);

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
    if (!host.Startup("daveshot", 1280, 800, !background, error))
    {
        ShowStartupFailure(error);
        instance::Release();
        return 1;
    }

    // No tray is not a failure: it means the close button quits, as it
    // did before there was one. The settings panel says so.
    std::string trayError;
    state.trayAvailable = tray::Create(trayError);
    if (!state.trayAvailable)
        SDL_Log("daveshot: %s", trayError.c_str());

    // A background start with nowhere to be in the background is an
    // ordinary start.
    state.inTray = background && state.trayAvailable;
    if (background && !state.inTray)
        host.Show();

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

        if (host.TakeCloseRequest())
            HandleCloseButton(state);
        if (instance::TakeWakeup())
            state.openRequested = true;

        ApplyHotkeys(state);
        HandleHotkeys(state);
        HandleTray(state);
        RefreshPrintKey(state);
        ApplyIntents(state, effects, now);
        TickCapture(state, effects, now);
        RefreshAutostart(state);

        // A problem is shown in a modal, and a modal needs the window.
        if (state.inTray && !state.error.empty())
        {
            host.Show();
            state.inTray = false;
        }

        // Nothing to draw while the window is away, and nothing to wait on
        // but the next hotkey, tray click or knock from a second launch.
        // The capture sequence is the exception: it hides the window and
        // then times its next step, so it has to keep ticking.
        if (state.inTray && !CaptureInProgress(state))
        {
            host.WaitForEvent(kDormantWaitMs);
            continue;
        }

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
    tray::Destroy();
    hotkeys::Shutdown();
    host.Shutdown();
    instance::Release();
    return 0;
}
}
