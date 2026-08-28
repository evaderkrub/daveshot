#include "ui/UiRoot.h"

#include "app/AppState.h"
#include "app/CaptureFlow.h"
#include "ui/AboutDialog.h"
#include "ui/IconsMaterialDesign.h"
#include "ui/Textures.h"

#include "imgui.h"
#include "imgui_internal.h"   // DockBuilder, for the first-run layout

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

        const bool idle = !CaptureInProgress(state);

        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem(ICON_MD_CROP "  Capture region###MenuRegion",
                                state.settings.hotkeyRegion.c_str(), false, idle))
            {
                state.request.mode = CaptureMode::Region;
                state.pendingRequest = true;
            }
            if (ImGui::MenuItem(ICON_MD_DESKTOP_WINDOWS "  Capture full screen###MenuFullScreen",
                                state.settings.hotkeyScreen.c_str(), false, idle))
            {
                state.request.mode = CaptureMode::FullScreen;
                state.pendingRequest = true;
            }

            ImGui::Separator();

            const bool haveShot = CurrentShot(state) != nullptr;
            if (ImGui::MenuItem(ICON_MD_SAVE "  Save###MenuSave", "Ctrl+S", false, haveShot))
                state.saveRequested = true;
            if (ImGui::MenuItem(ICON_MD_CONTENT_COPY "  Copy###MenuCopy", "Ctrl+C", false, haveShot))
                state.copyRequested = true;

            ImGui::Separator();
            if (ImGui::MenuItem(ICON_MD_CLOSE "  Quit###Quit", "Alt+F4"))
                state.quitRequested = true;
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            ImGui::MenuItem(kWindowSettings, nullptr, &state.showSettings);
            ImGui::MenuItem(kWindowHistory, nullptr, &state.showHistory);
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

    // Without this the panels come up floating over the dockspace on a first
    // run, and since WindowBg and DockingEmptyBg are the same colour with no
    // window border, they read as text scattered on an empty background.
    void BuildDefaultLayout(ImGuiID dockspaceId)
    {
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->WorkSize);

        ImGuiID centre = dockspaceId;
        ImGuiID left   = 0;
        ImGuiID right  = 0;
        ImGuiID bottom = 0;

        ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left,  0.24f, &left,   &centre);
        ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.28f, &right,  &centre);
        ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down,  0.26f, &bottom, &centre);

        ImGui::DockBuilderDockWindow(kWindowCapture,  left);
        ImGui::DockBuilderDockWindow(kWindowPreview,  centre);
        ImGui::DockBuilderDockWindow(kWindowSettings, right);
        ImGui::DockBuilderDockWindow(kWindowHistory,  bottom);
        ImGui::DockBuilderFinish(dockspaceId);
    }

    // Errors from below the interface arrive as a string on the state;
    // showing them is the interface's job, which is why nothing under
    // src/app or src/platform throws.
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
        if (ImGui::Button("OK") || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            state.error.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Window-level shortcuts, distinct from the global hotkeys: these work
    // when daveshot has focus and do not need registering with the OS.
    void HandleShortcuts(AppState& state)
    {
        if (CurrentShot(state) == nullptr)
            return;
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
            state.saveRequested = true;
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C))
            state.copyRequested = true;
    }
}

void Draw(AppState& state, TextureCache& textures)
{
    // The overlay owns the whole window while a region is being chosen.
    // Drawing the panels underneath would let a click fall through to a
    // button the user cannot see.
    if (state.phase == CapturePhase::Selecting)
    {
        const OverlayResult overlay = DrawRegionOverlay(state, textures);
        if (overlay.cancelled)
            state.cancelRequested = true;
        else if (overlay.committed)
            state.pendingSelection = true;
        return;
    }

    DrawMenuBar(state);
    HandleShortcuts(state);

    const ImGuiID dockspaceId = ImGui::GetID("DaveshotDockspace");
    ImGui::DockSpaceOverViewport(dockspaceId, nullptr, ImGuiDockNodeFlags_AutoHideTabBar);

    // Has to run after DockSpaceOverViewport created the node and before the
    // windows below are begun.
    if (state.layoutPending)
    {
        state.layoutPending = false;
        BuildDefaultLayout(dockspaceId);
    }

    DrawCapturePanel(state);
    DrawPreviewPanel(state, textures);
    DrawHistoryPanel(state, textures);
    DrawSettingsPanel(state);

    if (state.showDemo)
        ImGui::ShowDemoWindow(&state.showDemo);

    DrawAboutDialog(state);
    DrawErrorDialog(state);
}
}
