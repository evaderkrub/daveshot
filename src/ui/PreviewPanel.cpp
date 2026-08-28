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
    // Fits the image inside the space left in the panel, never enlarging it.
    // A 200x80 capture shown at 900 pixels wide would be a wall of blur; the
    // useful thing is to see it at its real size and know that is its real
    // size.
    ImVec2 FitSize(const Image& image, const ImVec2& available)
    {
        const float width  = (float)image.width;
        const float height = (float)image.height;
        if (width <= 0.0f || height <= 0.0f)
            return ImVec2(0.0f, 0.0f);

        float scale = 1.0f;
        if (available.x > 0.0f && width > available.x)
            scale = available.x / width;
        if (available.y > 0.0f && height * scale > available.y)
            scale = available.y / height;
        if (scale > 1.0f)
            scale = 1.0f;

        return ImVec2(width * scale, height * scale);
    }

    void DrawEmptyState()
    {
        const theme::Palette& palette = theme::Current();
        ImGui::PushStyleColor(ImGuiCol_Text, palette.textFaint);
        ImGui::PushFont(nullptr, ImGui::GetFontSize() * 2.4f);
        ImGui::TextUnformatted(ICON_MD_PHOTO_CAMERA);
        ImGui::PopFont();
        ImGui::PopStyleColor();

        ImGui::TextDisabled("Nothing captured yet.");
        ImGui::TextDisabled("Press the Region button, or use the hotkey from anywhere.");
    }
}

void DrawPreviewPanel(AppState& state, TextureCache& textures)
{
    ImGui::SetNextWindowSize(ImVec2(760, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(kWindowPreview))
    {
        ImGui::End();
        return;
    }

    Shot* shot = CurrentShot(state);
    if (shot == nullptr)
    {
        DrawEmptyState();
        ImGui::End();
        return;
    }

    // Toolbar first, so the buttons keep their place as the image below
    // changes size from one capture to the next.
    if (ImGui::Button(ICON_MD_CONTENT_COPY "  Copy###CopyShot"))
        state.copyRequested = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Put the picture itself on the clipboard");

    // Copying the path is a different job from copying the picture: it is
    // what you want when the file is going into a message, a command line or
    // a bug report rather than into a document.
    ImGui::SameLine();
    ImGui::BeginDisabled(shot->savedPath.empty());
    if (ImGui::Button(ICON_MD_LINK "  Copy path###CopyPath"))
        state.copyPathRequested = true;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(shot->savedPath.empty()
                          ? "Save it first -- there is no path yet"
                          : "Copy the file path as text");
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button(ICON_MD_SAVE "  Save###SaveShot"))
        state.saveRequested = true;

    ImGui::SameLine();
    ImGui::BeginDisabled(shot->savedPath.empty());
    if (ImGui::Button(ICON_MD_FOLDER_OPEN "  Show in folder###RevealShot"))
        state.revealRequested = true;
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::TextDisabled("%s  -  %02d:%02d:%02d",
                        shot->label.c_str(),
                        shot->when.hour, shot->when.minute, shot->when.second);

    if (!shot->savedPath.empty())
    {
        ImGui::PushFont(nullptr, ImGui::GetFontSize() * 0.9f);
        ImGui::TextDisabled("%s", shot->savedPath.c_str());
        ImGui::PopFont();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", shot->savedPath.c_str());
    }
    else
    {
        ImGui::TextDisabled("Not saved yet");
    }

    ImGui::Separator();

    const ImTextureID texture = textures.Get(shot->id, kSlotFull, shot->image);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 size = FitSize(shot->image, available);

    if (texture != 0 && size.x >= 1.0f && size.y >= 1.0f)
    {
        // Centre it: a capture narrower than the panel pinned to the left
        // looks like a layout mistake rather than a deliberate size.
        const float offset = (available.x - size.x) * 0.5f;
        if (offset > 0.0f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);

        ImGui::Image(texture, size);
    }
    else
    {
        // No renderer (the end-to-end tests) or an upload that failed. Say so
        // rather than showing an empty panel that looks like a lost capture.
        ImGui::TextDisabled("%dx%d, %.1f MB",
                            shot->image.width, shot->image.height,
                            (double)shot->image.ByteSize() / (1024.0 * 1024.0));
    }

    ImGui::End();
}
}
