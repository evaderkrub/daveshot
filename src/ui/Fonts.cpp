#include "ui/Fonts.h"

#include "platform/Paths.h"
#include "ui/IconsMaterialDesign.h"

#include "imgui.h"

namespace daveshot::fonts
{
namespace
{
    ImFont* gUi       = nullptr;
    ImFont* gUiBold   = nullptr;
    ImFont* gUiItalic = nullptr;
    ImFont* gMono     = nullptr;
    float   gBaseSize = 17.0f;

    const char* kUiRegular  = "fonts/OpenSans-Regular.ttf";
    const char* kUiSemiBold = "fonts/OpenSans-SemiBold.ttf";
    const char* kUiItalic   = "fonts/OpenSans-Italic.ttf";
    const char* kMono       = "fonts/FiraCode-Regular.ttf";
    // Material Icons is an icon set that happens to ship as a font, so it
    // lives under assets/icons rather than assets/fonts.
    const char* kIcons      = "icons/MaterialIcons-Regular.ttf";

    // Merges the Material Icons block into whichever face was added last.
    void MergeIcons(float sizePixels)
    {
        const std::string path = paths::Asset(kIcons);
        if (!paths::Exists(path))
            return;

        ImFontConfig cfg;
        cfg.MergeMode  = true;
        cfg.PixelSnapH = true;
        // Icons are square glyphs sitting on a text baseline: force a full-em
        // advance so a row of them lines up, and nudge them down, because
        // otherwise they ride high inside a framed button.
        cfg.GlyphMinAdvanceX = sizePixels;
        cfg.GlyphOffset      = ImVec2(0.0f, sizePixels * 0.20f);
        ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), sizePixels, &cfg);
    }

    ImFont* AddFace(const char* relative, float sizePixels, std::string& missing)
    {
        const std::string path = paths::Asset(relative);
        if (!paths::Exists(path))
        {
            if (!missing.empty()) missing += ", ";
            missing += relative;
            return nullptr;
        }
        return ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), sizePixels);
    }
}

bool Load(float baseSizePixels, std::string& error)
{
    ImGuiIO& io = ImGui::GetIO();
    gBaseSize = baseSizePixels;

    std::string missing;

    gUi = AddFace(kUiRegular, baseSizePixels, missing);
    if (gUi) MergeIcons(baseSizePixels);

    gUiBold = AddFace(kUiSemiBold, baseSizePixels, missing);
    if (gUiBold) MergeIcons(baseSizePixels);

    gUiItalic = AddFace(kUiItalic, baseSizePixels, missing);
    gMono     = AddFace(kMono, baseSizePixels, missing);

    // Anything that failed falls back to the body face, and the body face
    // falls back to ImGui's built-in one, so callers never have to null-check.
    if (gUi == nullptr)
        gUi = io.Fonts->AddFontDefault();
    if (gUiBold   == nullptr) gUiBold   = gUi;
    if (gUiItalic == nullptr) gUiItalic = gUi;
    if (gMono     == nullptr) gMono     = gUi;

    io.FontDefault = gUi;

    // Since 1.92 the style carries the base size and the scale separately;
    // the theme sets FontScaleMain, so the two multiply cleanly.
    ImGui::GetStyle().FontSizeBase = baseSizePixels;

    if (!missing.empty())
    {
        error = "missing font files under assets: " + missing;
        return false;
    }
    return true;
}

ImFont* Ui()       { return gUi; }
ImFont* UiBold()   { return gUiBold; }
ImFont* UiItalic() { return gUiItalic; }
ImFont* Mono()     { return gMono; }
float   BaseSize() { return gBaseSize; }
}
