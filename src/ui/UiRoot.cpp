#include "ui/UiRoot.h"

#include "app/AppState.h"
#include "daveshot/Version.h"
#include "ui/AboutDialog.h"
#include "ui/Fonts.h"
#include "ui/IconsMaterialDesign.h"
#include "ui/Theme.h"

#include "imgui.h"

#include <cstdio>

namespace daveshot::ui
{
// Labels carry an icon glyph, and ImGui derives an item's identifier from the
// whole label -- so every label the tests address gets an explicit "###id"
// suffix. Without it the tests would have to spell out icon codepoints, and
// swapping an icon would break them.
namespace
{
    void DrawMenuBar(AppState& state)
    {
        if (!ImGui::BeginMainMenuBar())
            return;

        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem(ICON_MD_PHOTO_CAMERA "  Capture###Capture", "PrtScn", false, false))
            {
                // Deliberately disabled: capture is the next piece of work.
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_MD_CLOSE "  Quit###Quit", "Alt+F4"))
                state.quitRequested = true;
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            ImGui::MenuItem(kWindowWorkspace, nullptr, false, false);
            ImGui::MenuItem(kWindowSettings, nullptr, &state.showSettings);
            ImGui::Separator();
            ImGui::MenuItem("ImGui Demo", nullptr, &state.showDemo);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem(ICON_MD_INFO "  About###About"))
                state.showAbout = true;
            ImGui::EndMenu();
        }

        // Status sits right-aligned in the menu bar rather than in a separate
        // status bar: it costs no vertical space and stays visible whatever
        // the docking layout is.
        const float width = ImGui::CalcTextSize(state.status.c_str()).x;
        ImGui::SameLine(ImGui::GetWindowWidth() - width - ImGui::GetStyle().ItemSpacing.x * 2.0f);
        ImGui::TextDisabled("%s", state.status.c_str());

        ImGui::EndMainMenuBar();
    }

    void DrawSettingsWindow(AppState& state)
    {
        if (!state.showSettings)
            return;

        ImGui::SetNextWindowSize(ImVec2(360, 260), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(kWindowSettings, &state.showSettings))
        {
            ImGui::SeparatorText("Appearance");

            int themeIndex = theme::CurrentIndex();
            if (ImGui::BeginCombo("Theme", theme::At(themeIndex).name))
            {
                for (int i = 0; i < theme::Count(); ++i)
                {
                    const bool selected = (i == themeIndex);
                    if (ImGui::Selectable(theme::At(i).name, selected))
                        SetTheme(state, theme::At(i).name);
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            // The slider writes through SetUiScale so the style-rebuild flag
            // is raised in exactly one place; the rebuild itself happens
            // between frames, never here.
            float scale = state.settings.uiScale;
            if (ImGui::SliderFloat("UI scale", &scale,
                                   Settings::kMinScale, Settings::kMaxScale, "%.2fx"))
                SetUiScale(state, scale);

            ImGui::SameLine();
            if (ImGui::SmallButton("Reset##scale"))
                SetUiScale(state, 1.0f);

            ImGui::Spacing();
            ImGui::TextDisabled("Fonts loaded at %.0f px, drawn at %.0f px",
                                (double)fonts::BaseSize(),
                                (double)(fonts::BaseSize() * state.settings.uiScale));
        }
        ImGui::End();
    }

    void DrawWorkspaceWindow(AppState& state)
    {
        ImGui::SetNextWindowSize(ImVec2(520, 320), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(kWindowWorkspace))
        {
            ImGui::PushFont(fonts::UiBold(), 0.0f);
            ImGui::TextUnformatted(ICON_MD_PHOTO_CAMERA "  daveshot");
            ImGui::PopFont();

            ImGui::TextWrapped("%s", kAppSummary);
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextDisabled("Nothing is captured yet -- this build is the "
                                "shell: window, docking, theming, fonts, "
                                "settings and the test harness.");

            ImGui::Spacing();
            if (ImGui::Button(ICON_MD_PHOTO_CAMERA "  Capture###Capture"))
                state.status = "Capture is not implemented yet";
            ImGui::SameLine();
            if (ImGui::Button(ICON_MD_INFO "  About###About"))
                state.showAbout = true;

            ImGui::Spacing();
            ImGui::PushFont(fonts::Mono(), 0.0f);
            ImGui::TextUnformatted("assets and settings resolve from the exe folder");
            ImGui::PopFont();
        }
        ImGui::End();
    }

    // Errors from below the UI arrive as a string on the state; showing them
    // is the interface's job, which is why nothing under src/app or
    // src/platform throws.
    void DrawErrorDialog(AppState& state)
    {
        if (!state.error.empty() && !ImGui::IsPopupOpen("###DaveshotError"))
            ImGui::OpenPopup("###DaveshotError");

        const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (!ImGui::BeginPopupModal(ICON_MD_ERROR "  Problem###DaveshotError",
                                    nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
        ImGui::TextUnformatted(state.error.c_str());
        ImGui::PopTextWrapPos();

        ImGui::Separator();
        if (ImGui::Button("OK"))
        {
            state.error.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void Draw(AppState& state)
{
    DrawMenuBar(state);

    // Docking host for everything below. AutoHideTabBar keeps a single
    // undocked panel from growing a pointless tab strip.
    ImGui::DockSpaceOverViewport(ImGui::GetID("DaveshotDockspace"), nullptr,
                                 ImGuiDockNodeFlags_AutoHideTabBar);

    DrawWorkspaceWindow(state);
    DrawSettingsWindow(state);

    if (state.showDemo)
        ImGui::ShowDemoWindow(&state.showDemo);

    DrawAboutDialog(state);
    DrawErrorDialog(state);
}
}
