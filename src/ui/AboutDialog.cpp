#include "ui/AboutDialog.h"

#include "app/AppState.h"
#include "daveshot/Version.h"
#include "platform/Paths.h"
#include "ui/IconsMaterialDesign.h"
#include "ui/Theme.h"

#include "imgui.h"

namespace daveshot::ui
{
namespace
{
    const char* kAboutTitle = ICON_MD_INFO "  About daveshot###AboutDaveshot";
}

void DrawAboutDialog(AppState& state)
{
    // OpenPopup has to be called from inside the same ID stack the modal is
    // begun in, and only once -- calling it every frame re-centres the window
    // and swallows the close.
    if (state.showAbout && !ImGui::IsPopupOpen("###AboutDaveshot"))
        ImGui::OpenPopup("###AboutDaveshot");

    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (!ImGui::BeginPopupModal(kAboutTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
    ImGui::TextUnformatted(kAppName);
    ImGui::Text("Version %s", kAppVersion);
    ImGui::Spacing();
    ImGui::TextUnformatted(kAppSummary);
    ImGui::PopTextWrapPos();

    ImGui::Separator();

    ImGui::TextDisabled("Theme");
    ImGui::SameLine();
    ImGui::TextUnformatted(theme::At(theme::CurrentIndex()).name);

    ImGui::TextDisabled("UI scale");
    ImGui::SameLine();
    ImGui::Text("%.0f%%", (double)(state.settings.uiScale * 100.0f));

    // The install folder is the first thing worth knowing when a report says
    // "it did not find its assets".
    ImGui::TextDisabled("Folder");
    ImGui::SameLine();
    ImGui::TextUnformatted(paths::ExeDir().c_str());

    ImGui::Separator();

    if (ImGui::Button("Copy details"))
    {
        const std::string details =
            std::string(kAppName) + " " + kAppVersion + "\n" + paths::ExeDir();
        ImGui::SetClipboardText(details.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        state.showAbout = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}
}
