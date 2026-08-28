#include "ui/Panels.h"

#include "app/AppState.h"
#include "ui/IconsMaterialDesign.h"
#include "ui/Theme.h"

#include "imgui.h"

#include <cstdio>

namespace daveshot::ui
{
namespace
{
    // A capture button. Wide, obvious, and disabled as one group while a
    // capture is running -- a second request mid-countdown is never what
    // anyone means.
    bool CaptureButton(const char* label, const char* tooltip, bool enabled)
    {
        ImGui::BeginDisabled(!enabled);
        const float width = ImGui::GetContentRegionAvail().x;
        const bool clicked = ImGui::Button(label, ImVec2(width, 0.0f));
        ImGui::EndDisabled();

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && tooltip != nullptr)
            ImGui::SetTooltip("%s", tooltip);
        return clicked;
    }

    void DrawCountdown(const AppState& state)
    {
        const int left = CountdownRemaining(state, ImGui::GetTime());
        if (left <= 0)
            return;

        const theme::Palette& palette = theme::Current();
        ImGui::PushStyleColor(ImGuiCol_Text, palette.accent);
        ImGui::PushFont(nullptr, ImGui::GetFontSize() * 2.0f);
        ImGui::Text("%d", left);
        ImGui::PopFont();
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("until capture");
    }

    void DrawMonitorPicker(AppState& state)
    {
        if (state.monitors.empty())
        {
            ImGui::TextDisabled("No monitors reported");
            return;
        }

        int index = state.request.monitorIndex;
        if (index < 0 || index >= (int)state.monitors.size())
            index = 0;

        if (ImGui::BeginCombo("Monitor", state.monitors[(size_t)index].name.c_str()))
        {
            for (int i = 0; i < (int)state.monitors.size(); ++i)
            {
                const bool selected = (i == index);
                if (ImGui::Selectable(state.monitors[(size_t)i].name.c_str(), selected))
                    state.request.monitorIndex = i;
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    void DrawWindowPicker(AppState& state)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Window");
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_MD_REFRESH "  Refresh###RefreshWindows"))
            state.windowListStale = true;

        if (state.windows.empty())
        {
            ImGui::TextDisabled("No capturable windows found");
            return;
        }

        // A list rather than a combo: window titles are long, there are
        // usually a dozen, and picking one is the whole interaction.
        const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
        if (ImGui::BeginChild("##WindowList", ImVec2(0.0f, rowHeight * 6.0f),
                              ImGuiChildFlags_Borders))
        {
            for (const screen::WindowInfo& window : state.windows)
            {
                const bool selected = (state.request.windowHandle == window.handle);

                char row[320];
                std::snprintf(row, sizeof(row), "%s##win%llu",
                              window.title.c_str(), (unsigned long long)window.handle);

                if (ImGui::Selectable(row, selected))
                    state.request.windowHandle = window.handle;

                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s\n%s  -  %dx%d",
                                      window.title.c_str(),
                                      window.process.empty() ? "unknown program"
                                                             : window.process.c_str(),
                                      window.bounds.w, window.bounds.h);
            }
        }
        ImGui::EndChild();
    }
}

void DrawCapturePanel(AppState& state)
{
    ImGui::SetNextWindowSize(ImVec2(340, 460), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(kWindowCapture))
    {
        ImGui::End();
        return;
    }

    const bool idle = !CaptureInProgress(state);

    // Region first and biggest: it is what people reach for, and putting it
    // at the top means the common case needs no aiming.
    if (CaptureButton(ICON_MD_CROP "  Region###CaptureRegion",
                      "Freeze the desktop and drag out an area", idle))
    {
        state.request.mode = CaptureMode::Region;
        state.pendingRequest = true;
    }

    ImGui::Spacing();

    if (CaptureButton(ICON_MD_DESKTOP_WINDOWS "  Full screen###CaptureFullScreen",
                      "Every monitor, as one image", idle))
    {
        state.request.mode = CaptureMode::FullScreen;
        state.pendingRequest = true;
    }

    if (CaptureButton(ICON_MD_MONITOR "  This monitor###CaptureMonitor",
                      "One monitor, whole", idle))
    {
        state.request.mode = CaptureMode::Monitor;
        state.pendingRequest = true;
    }

    const bool haveWindow = state.request.windowHandle != 0;
    if (CaptureButton(ICON_MD_WEB_ASSET "  Window###CaptureWindow",
                      haveWindow ? "Capture the selected window, even where it is covered"
                                 : "Choose a window from the list below first",
                      idle && haveWindow))
    {
        state.request.mode = CaptureMode::Window;
        state.pendingRequest = true;
    }

    ImGui::Spacing();
    DrawCountdown(state);

    if (!idle && ImGui::Button(ICON_MD_CANCEL "  Cancel###CancelCapture"))
        state.cancelRequested = true;

    ImGui::SeparatorText("Options");

    int delay = state.request.delaySeconds;
    if (ImGui::SliderInt("Delay", &delay, 0, 10, delay == 0 ? "none" : "%d s"))
        state.request.delaySeconds = ClampDelaySeconds(delay);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Wait before capturing, so you can open a menu or hover something");

    DrawMonitorPicker(state);

    ImGui::SeparatorText("Windows");
    DrawWindowPicker(state);

    ImGui::End();
}
}
