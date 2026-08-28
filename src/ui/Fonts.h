#pragma once

#include <string>

struct ImFont;

// Font faces, loaded from assets/fonts beside the executable. Carried over
// from fwcom: Open Sans for UI text with Material Icons merged in so an icon
// can be written inline in any string, and Fira Code for anything whose
// columns depend on a fixed advance width.
namespace daveshot::fonts
{
    // Loads every face into the current ImGui context's atlas.
    //
    // Returns false with an error string when a font file is missing -- the
    // application is still usable in that state (ImGui's built-in font takes
    // over), so the caller should show the message rather than give up.
    bool Load(float baseSizePixels, std::string& error);

    ImFont* Ui();
    ImFont* UiBold();
    ImFont* UiItalic();
    ImFont* Mono();

    // The size the faces were loaded at. Scaling happens through the style's
    // FontScaleMain rather than by reloading at a new size.
    float BaseSize();
}
