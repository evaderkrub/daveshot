#include "ui/AppLoop.h"

#include "app/AppState.h"
#include "daveshot/Version.h"
#include "platform/Host.h"
#include "platform/Paths.h"
#include "ui/Fonts.h"
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

    Host host;
    if (!host.Startup("daveshot", 1280, 800, error))
    {
        ShowStartupFailure(error);
        return 1;
    }

    std::string fontError;
    if (!fonts::Load(kBaseFontSize, fontError) && state.error.empty())
        ReportError(state, fontError);

    while (host.PumpEvents() && !state.quitRequested)
    {
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

        host.BeginFrame();
        Draw(state);

        const ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        host.EndFrame(bg.x, bg.y, bg.z);
    }

    std::string saveError;
    if (!SaveSettings(state.settingsPath, state.settings, saveError))
    {
        // Nothing left to draw the message into; the window is on its way out.
        SDL_Log("daveshot: %s", saveError.c_str());
    }

    host.Shutdown();
    return 0;
}
}
