#include "ui/Panels.h"

#include "app/AppState.h"
#include "ui/IconsMaterialDesign.h"
#include "ui/Textures.h"
#include "ui/Theme.h"

#include "imgui.h"

#include <cstdio>

namespace daveshot::ui
{
namespace
{
    constexpr float kThumbHeight = 84.0f;

    ImVec2 ThumbSize(const Image& thumbnail)
    {
        if (thumbnail.height <= 0)
            return ImVec2(kThumbHeight, kThumbHeight);
        const float scale = kThumbHeight / (float)thumbnail.height;
        return ImVec2((float)thumbnail.width * scale, kThumbHeight);
    }
}

void DrawHistoryPanel(AppState& state, TextureCache& textures)
{
    if (!state.showHistory)
        return;

    ImGui::SetNextWindowSize(ImVec2(760, 150), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(kWindowHistory, &state.showHistory))
    {
        ImGui::End();
        return;
    }

    if (state.history.shots.empty())
    {
        ImGui::TextDisabled("Captures from this session appear here.");
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("%d of %d  -  %.0f MB",
                        (int)state.history.shots.size(),
                        state.history.limit,
                        (double)state.history.TotalBytes() / (1024.0 * 1024.0));
    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_MD_DELETE "  Clear###ClearHistory"))
    {
        state.history.Clear();
        state.selectedShotId = 0;
    }

    // One horizontal strip. Scrolls sideways rather than wrapping, so the
    // panel works docked as a short bar along the bottom -- which is where it
    // ends up in the default layout.
    if (ImGui::BeginChild("##HistoryStrip", ImVec2(0.0f, 0.0f),
                          ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar))
    {
        const Shot* current = CurrentShot(state);
        const theme::Palette& palette = theme::Current();

        for (const Shot& shot : state.history.shots)
        {
            ImGui::PushID((int)shot.id);

            const ImTextureID texture = textures.Get(shot.id, kSlotThumbnail, shot.thumbnail);
            const ImVec2 size = ThumbSize(shot.thumbnail);
            const bool selected = (current != nullptr && current->id == shot.id);

            // The selected shot gets an accent frame. Without it the strip
            // gives no clue which one the preview is showing.
            if (selected)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, palette.accentFill);
                ImGui::PushStyleColor(ImGuiCol_Border, palette.accent);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
            }

            bool clicked = false;
            if (texture != 0)
                clicked = ImGui::ImageButton("##thumb", texture, size);
            else
                clicked = ImGui::Button(shot.label.c_str(), ImVec2(size.x, size.y));

            if (selected)
            {
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(2);
            }

            if (clicked)
                state.selectedShotId = shot.id;

            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s\n%02d:%02d:%02d\n%s",
                                  shot.label.c_str(),
                                  shot.when.hour, shot.when.minute, shot.when.second,
                                  shot.savedPath.empty() ? "not saved"
                                                         : shot.savedPath.c_str());
            }

            ImGui::PopID();
            ImGui::SameLine();
        }
    }
    ImGui::EndChild();

    ImGui::End();
}
}
