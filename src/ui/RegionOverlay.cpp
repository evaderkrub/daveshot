#include "ui/Panels.h"

#include "app/AppState.h"
#include "app/CaptureFlow.h"   // kMinSelection: what counts as a deliberate drag
#include "ui/Textures.h"
#include "ui/Theme.h"

#include "imgui.h"

#include <cstdio>

namespace daveshot::ui
{
namespace
{
    // Everything on screen is drawn at display scale, while the selection is
    // tracked in backdrop pixels. Keeping one conversion in one place is what
    // stops a high-DPI display from producing a selection that does not match
    // the rectangle the user dragged.
    struct Mapping
    {
        float toImageX = 1.0f;
        float toImageY = 1.0f;
        float toScreenX = 1.0f;
        float toScreenY = 1.0f;

        ImVec2 ToScreen(int imageX, int imageY) const
        {
            return ImVec2((float)imageX * toScreenX, (float)imageY * toScreenY);
        }
    };

    Mapping BuildMapping(const Image& backdrop, const ImVec2& display)
    {
        Mapping map;
        if (backdrop.width > 0 && display.x > 0.0f)
        {
            map.toImageX  = (float)backdrop.width / display.x;
            map.toScreenX = display.x / (float)backdrop.width;
        }
        if (backdrop.height > 0 && display.y > 0.0f)
        {
            map.toImageY  = (float)backdrop.height / display.y;
            map.toScreenY = display.y / (float)backdrop.height;
        }
        return map;
    }

    // The smallest window containing the point, so a dialog on top of its
    // parent wins rather than the parent swallowing the hit.
    const screen::WindowInfo* WindowUnder(const AppState& state, int screenX, int screenY)
    {
        const screen::WindowInfo* best = nullptr;
        for (const screen::WindowInfo& window : state.windows)
        {
            if (!window.bounds.Contains(screenX, screenY))
                continue;
            if (best == nullptr || window.bounds.Area() < best->bounds.Area())
                best = &window;
        }
        return best;
    }

    void DimAround(ImDrawList* draw, const ImVec2& display, const ImVec2& a, const ImVec2& b)
    {
        const ImU32 dim = IM_COL32(0, 0, 0, 130);
        // Four bands rather than one full-screen quad with a hole: ImGui has
        // no hole primitive, and drawing the image twice costs more than four
        // rectangles.
        draw->AddRectFilled(ImVec2(0, 0), ImVec2(display.x, a.y), dim);
        draw->AddRectFilled(ImVec2(0, b.y), display, dim);
        draw->AddRectFilled(ImVec2(0, a.y), ImVec2(a.x, b.y), dim);
        draw->AddRectFilled(ImVec2(b.x, a.y), ImVec2(display.x, b.y), dim);
    }

    void DrawReadout(ImDrawList* draw, const ImVec2& display, const ImVec2& anchor,
                     const char* text, ImU32 background, ImU32 foreground)
    {
        const ImVec2 padding(8.0f, 5.0f);
        const ImVec2 size = ImGui::CalcTextSize(text);

        // Keep the label on screen: near the bottom or right edge it flips to
        // the other side of the cursor rather than running off.
        ImVec2 position = anchor;
        if (position.x + size.x + padding.x * 2.0f > display.x)
            position.x = display.x - size.x - padding.x * 2.0f;
        if (position.y + size.y + padding.y * 2.0f > display.y)
            position.y = display.y - size.y - padding.y * 2.0f;
        if (position.x < 0.0f) position.x = 0.0f;
        if (position.y < 0.0f) position.y = 0.0f;

        draw->AddRectFilled(position,
                            ImVec2(position.x + size.x + padding.x * 2.0f,
                                   position.y + size.y + padding.y * 2.0f),
                            background, 4.0f);
        draw->AddText(ImVec2(position.x + padding.x, position.y + padding.y),
                      foreground, text);
    }
}

OverlayResult DrawRegionOverlay(AppState& state, TextureCache& textures)
{
    OverlayResult result;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 display = viewport->Size;

    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(display);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 255));

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
                                 | ImGuiWindowFlags_NoMove
                                 | ImGuiWindowFlags_NoSavedSettings
                                 | ImGuiWindowFlags_NoBringToFrontOnFocus
                                 | ImGuiWindowFlags_NoNavFocus
                                 | ImGuiWindowFlags_NoScrollbar;

    if (!ImGui::Begin("##RegionOverlay", nullptr, flags))
    {
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        return result;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const Mapping map = BuildMapping(state.backdrop, display);
    const theme::Palette& palette = theme::Current();

    // The frozen desktop, filling the overlay.
    const ImTextureID backdrop = textures.Get(kBackdropShotId, kSlotBackdrop, state.backdrop);
    if (backdrop != 0)
        draw->AddImage(backdrop, ImVec2(0.0f, 0.0f), display);

    const ImVec2 mouse = ImGui::GetMousePos();
    const int mouseImageX = (int)(mouse.x * map.toImageX);
    const int mouseImageY = (int)(mouse.y * map.toImageY);

    // --- Selection tracking ------------------------------------------------
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        state.selecting     = true;
        state.selectAnchorX = mouseImageX;
        state.selectAnchorY = mouseImageY;
        state.selection     = Rect{ mouseImageX, mouseImageY, 0, 0 };
    }
    else if (state.selecting && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        state.selection = RectFromCorners(state.selectAnchorX, state.selectAnchorY,
                                          mouseImageX, mouseImageY);
    }
    else if (state.selecting && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        state.selection = RectFromCorners(state.selectAnchorX, state.selectAnchorY,
                                          mouseImageX, mouseImageY);

        // A click with no drag means "that window", using the list gathered
        // before the overlay went up -- the overlay is on top of everything
        // now, so asking the OS what is under the cursor would just answer
        // "the overlay".
        if (state.selection.w < kMinSelection || state.selection.h < kMinSelection)
        {
            const screen::WindowInfo* window =
                WindowUnder(state, mouseImageX + state.backdropBounds.x,
                            mouseImageY + state.backdropBounds.y);
            if (window != nullptr)
            {
                Rect area = window->bounds;
                area.x -= state.backdropBounds.x;
                area.y -= state.backdropBounds.y;
                state.selection = Intersect(area, Rect{ 0, 0, state.backdrop.width,
                                                        state.backdrop.height });
            }
        }

        state.selecting = false;
        result.committed = true;
    }

    // --- Painting ----------------------------------------------------------
    const bool dragging = state.selecting;
    const Rect area = state.selection;

    if (dragging && area.w > 0 && area.h > 0)
    {
        const ImVec2 a = map.ToScreen(area.x, area.y);
        const ImVec2 b = map.ToScreen(area.Right(), area.Bottom());

        DimAround(draw, display, a, b);
        draw->AddRect(a, b, palette.accent, 0.0f, 0, 2.0f);

        char text[64];
        std::snprintf(text, sizeof(text), "%d x %d", area.w, area.h);
        DrawReadout(draw, display, ImVec2(b.x + 12.0f, b.y + 12.0f), text,
                    IM_COL32(0, 0, 0, 200), palette.text);
    }
    else
    {
        // Nothing dragged yet: dim everything, and outline whichever window
        // a click would take.
        draw->AddRectFilled(ImVec2(0.0f, 0.0f), display, IM_COL32(0, 0, 0, 110));

        const screen::WindowInfo* hovered =
            WindowUnder(state, mouseImageX + state.backdropBounds.x,
                        mouseImageY + state.backdropBounds.y);
        if (hovered != nullptr)
        {
            const ImVec2 a = map.ToScreen(hovered->bounds.x - state.backdropBounds.x,
                                          hovered->bounds.y - state.backdropBounds.y);
            const ImVec2 b = map.ToScreen(hovered->bounds.Right() - state.backdropBounds.x,
                                          hovered->bounds.Bottom() - state.backdropBounds.y);
            draw->AddRect(a, b, palette.accent, 0.0f, 0, 2.0f);
        }

        // Crosshair, so the pointer can be placed precisely against an edge.
        draw->AddLine(ImVec2(0.0f, mouse.y), ImVec2(display.x, mouse.y),
                      IM_COL32(255, 255, 255, 60));
        draw->AddLine(ImVec2(mouse.x, 0.0f), ImVec2(mouse.x, display.y),
                      IM_COL32(255, 255, 255, 60));

        const char* hint = (hovered != nullptr)
                         ? "Drag to select  -  click to take this window  -  Esc to cancel"
                         : "Drag to select an area  -  Esc to cancel";
        DrawReadout(draw, display, ImVec2(mouse.x + 18.0f, mouse.y + 18.0f), hint,
                    IM_COL32(0, 0, 0, 200), palette.text);
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        result.cancelled = true;

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    return result;
}
}
