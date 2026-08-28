#include "ui/Theme.h"

#include <cmath>
#include <cstring>

namespace daveshot::theme
{
namespace
{
    constexpr ImU32 rgb(int r, int g, int b)         { return IM_COL32(r, g, b, 255); }
    constexpr ImU32 rgba(int r, int g, int b, int a) { return IM_COL32(r, g, b, a); }

    // Wili Dark note: its accent is red, so `danger` cannot rely on hue alone
    // to distinguish itself. Accent is the deeper red; danger is brighter and
    // more saturated.
    const Tokens kThemes[] = {
        // -- Slate Dark: the default. Neutral slate, blue accent. -----------
        { "Slate Dark", true,
          /* background   */ rgb( 22,  23,  27),
          /* surface      */ rgb( 25,  26,  31),
          /* surfaceInput */ rgb( 39,  41,  49),
          /* surfacePopup */ rgb( 30,  32,  38),
          /* border       */ rgb( 52,  56,  66),
          /* text         */ rgb(228, 230, 235),
          /* textMuted    */ rgb(150, 156, 168),
          /* textFaint    */ rgb(108, 114, 126),
          /* accent       */ rgb( 88, 140, 236),
          /* link         */ rgb(110, 168, 254),
          /* codeBg       */ rgb( 18,  19,  23),
          /* inlineCodeBg */ rgb( 48,  52,  64),
          /* inlineCodeFg */ rgb(236, 196, 141),
          /* selection    */ rgba( 88, 140, 236, 90),
          /* success      */ rgb(102, 187, 122),
          /* warning      */ rgb(226, 173,  88),
          /* danger       */ rgb(226, 106, 106) },

        // -- Wili Dark: fwcom's own palette. --------------------------------
        { "Wili Dark", true,
          rgb( 22,  23,  27), rgb( 25,  26,  31), rgb( 39,  41,  49),
          rgb( 30,  32,  38), rgb( 52,  56,  66),
          rgb(228, 230, 235), rgb(150, 156, 168), rgb(108, 114, 126),
          rgb(200,  48,  48), rgb(255, 120, 120),
          rgb( 18,  19,  23), rgb( 48,  52,  64), rgb(236, 196, 141),
          rgba(200,  48,  48,  90),
          rgb(102, 187, 122), rgb(226, 173,  88), rgb(255,  92,  92) },

        // -- Light ----------------------------------------------------------
        { "Light", false,
          rgb(247, 248, 250), rgb(255, 255, 255), rgb(236, 238, 242),
          rgb(255, 255, 255), rgb(213, 217, 224),
          rgb( 26,  28,  34), rgb( 82,  89, 102), rgb(138, 145, 158),
          rgb( 36, 100, 216), rgb( 26,  88, 198),
          rgb(243, 244, 248), rgb(232, 234, 240), rgb(166,  62,  22),
          rgba( 36, 100, 216,  70),
          rgb( 24, 132,  68), rgb(168, 106,   8), rgb(198,  48,  48) },

        // -- Midnight: near-black, easy on an OLED panel. -------------------
        { "Midnight", true,
          rgb(  8,   9,  12), rgb( 13,  14,  18), rgb( 25,  27,  33),
          rgb( 18,  20,  25), rgb( 38,  41,  50),
          rgb(226, 230, 238), rgb(140, 148, 162), rgb( 96, 103, 116),
          rgb( 96, 150, 250), rgb(118, 172, 255),
          rgb(  4,   5,   7), rgb( 34,  38,  48), rgb(240, 190, 130),
          rgba( 96, 150, 250,  90),
          rgb( 94, 190, 118), rgb(230, 176,  84), rgb(232, 100, 100) },

        // -- Nord -----------------------------------------------------------
        { "Nord", true,
          rgb( 46,  52,  64), rgb( 52,  58,  72), rgb( 59,  66,  82),
          rgb( 59,  66,  82), rgb( 67,  76,  94),
          rgb(216, 222, 233), rgb(166, 178, 196), rgb(126, 138, 158),
          rgb(136, 192, 208), rgb(143, 188, 187),
          rgb( 38,  43,  54), rgb( 67,  76,  94), rgb(235, 203, 139),
          rgba(136, 192, 208,  90),
          rgb(163, 190, 140), rgb(235, 203, 139), rgb(191,  97, 106) },
    };

    constexpr int kThemeCount = (int)(sizeof(kThemes) / sizeof(kThemes[0]));

    Palette  gPalette;
    int      gThemeIndex = 0;
    unsigned gApplyCount = 0;

    inline float Chan(ImU32 c, int shift) { return (float)((c >> shift) & 0xFF) / 255.0f; }

    // WCAG 2.x linearization.
    float SrgbToLinear(float c)
    {
        return (c <= 0.03928f) ? (c / 12.92f)
                               : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }

    float RelativeLuminance(ImU32 c)
    {
        const float r = SrgbToLinear(Chan(c, IM_COL32_R_SHIFT));
        const float g = SrgbToLinear(Chan(c, IM_COL32_G_SHIFT));
        const float b = SrgbToLinear(Chan(c, IM_COL32_B_SHIFT));
        return 0.2126f * r + 0.7152f * g + 0.0722f * b;
    }
}

int Count() { return kThemeCount; }

const Tokens& At(int index)
{
    if (index < 0)            index = 0;
    if (index >= kThemeCount) index = kThemeCount - 1;
    return kThemes[index];
}

int IndexByName(const char* name)
{
    if (name == nullptr)
        return 0;
    for (int i = 0; i < kThemeCount; ++i)
        if (std::strcmp(name, kThemes[i].name) == 0)
            return i;
    return 0;
}

ImU32 WithAlpha(ImU32 color, int alpha)
{
    return (color & ~IM_COL32_A_MASK) | ((ImU32)(alpha & 0xFF) << IM_COL32_A_SHIFT);
}

ImU32 Lift(ImU32 color, float amount, bool isDark)
{
    const float target = isDark ? 1.0f : 0.0f;
    int out[3];
    const int shifts[3] = { IM_COL32_R_SHIFT, IM_COL32_G_SHIFT, IM_COL32_B_SHIFT };
    for (int i = 0; i < 3; ++i)
    {
        float v = Chan(color, shifts[i]);
        v += (target - v) * amount;
        out[i] = (int)(v * 255.0f + 0.5f);
        if (out[i] < 0)   out[i] = 0;
        if (out[i] > 255) out[i] = 255;
    }
    const int a = (int)((color >> IM_COL32_A_SHIFT) & 0xFF);
    return IM_COL32(out[0], out[1], out[2], a);
}

float ContrastRatio(ImU32 a, ImU32 b)
{
    const float la = RelativeLuminance(a);
    const float lb = RelativeLuminance(b);
    const float hi = (la > lb) ? la : lb;
    const float lo = (la > lb) ? lb : la;
    return (hi + 0.05f) / (lo + 0.05f);
}

Palette Build(const Tokens& t)
{
    Palette p;
    const bool d = t.isDark;

    p.text      = t.text;
    p.textMuted = t.textMuted;
    p.textFaint = t.textFaint;
    // Headings sit above body text in contrast on dark themes and below it on
    // light ones -- which is the direction each needs.
    p.heading   = Lift(t.text, 0.18f, d);
    p.rule      = t.border;
    p.quoteBar  = t.textFaint;
    p.link      = t.link;

    p.surface        = t.surface;
    p.surfaceHovered = Lift(t.surfaceInput, 0.12f, d);
    p.surfaceActive  = Lift(t.surfaceInput, 0.20f, d);
    p.border         = t.border;
    p.borderHovered  = Lift(t.border, 0.25f, d);

    p.codeBackground       = t.codeBackground;
    p.inlineCodeBackground = t.inlineCodeBackground;
    p.inlineCodeText       = t.inlineCodeText;

    p.accent        = t.accent;
    p.accentHovered = Lift(t.accent, 0.15f, d);
    p.accentActive  = Lift(t.accent, 0.28f, d);
    // Text drawn on top of an accent fill: pick whichever of the theme's own
    // extremes has more contrast against the accent, rather than assuming
    // white reads on every accent color.
    p.accentText = (ContrastRatio(t.accent, t.text) >= ContrastRatio(t.accent, t.background))
                 ? t.text : t.background;

    p.success = t.success;
    p.warning = t.warning;
    p.danger  = t.danger;
    p.neutral = t.textFaint;

    p.successFill = WithAlpha(t.success,   56);
    p.warningFill = WithAlpha(t.warning,   56);
    p.dangerFill  = WithAlpha(t.danger,    56);
    p.neutralFill = WithAlpha(t.textFaint, 40);
    p.accentFill  = WithAlpha(t.accent,    56);

    return p;
}

const Palette& Current()      { return gPalette; }
int            CurrentIndex() { return gThemeIndex; }
unsigned       ApplyCountForTests() { return gApplyCount; }

void Apply(int index, float sizeScale)
{
    ++gApplyCount;

    if (index < 0)            index = 0;
    if (index >= kThemeCount) index = kThemeCount - 1;
    gThemeIndex = index;

    const Tokens& t = kThemes[index];
    const bool d = t.isDark;
    gPalette = Build(t);

    // Start from a matching base so the handful of ImGui colors not named
    // below stay legible even if ImGui adds new ones in a future update.
    if (d) ImGui::StyleColorsDark(); else ImGui::StyleColorsLight();

    ImGuiStyle& s = ImGui::GetStyle();

    // --- Metrics. Assigned from constants (never read-modify-write), so a
    // --- re-apply lands on exactly the same numbers.
    s.WindowPadding     = ImVec2(12, 12);
    s.FramePadding      = ImVec2(10,  6);
    s.CellPadding       = ImVec2( 6,  4);
    s.ItemSpacing       = ImVec2( 8,  7);
    s.ItemInnerSpacing  = ImVec2( 6,  5);
    s.IndentSpacing     = 21.0f;
    s.ScrollbarSize     = 12.0f;
    s.GrabMinSize       = 10.0f;

    s.WindowBorderSize  = 0.0f;
    s.ChildBorderSize   = 1.0f;
    s.FrameBorderSize   = 0.0f;
    s.PopupBorderSize   = 1.0f;
    s.TabBorderSize     = 0.0f;

    s.WindowRounding    = 6.0f;
    s.ChildRounding     = 8.0f;
    s.FrameRounding     = 6.0f;
    s.PopupRounding     = 6.0f;
    s.ScrollbarRounding = 6.0f;
    s.GrabRounding      = 6.0f;
    s.TabRounding       = 6.0f;

    s.WindowTitleAlign  = ImVec2(0.0f, 0.5f);

    // ScaleAllSizes also scales the fields below. They are not otherwise part
    // of the design, so keep them at ImGui's own defaults -- but they MUST
    // still be assigned from constants here, or they silently carry the
    // previous apply's already-scaled value into ScaleAllSizes and compound
    // exactly like FramePadding would without its reset above.
    s.WindowMinSize          = ImVec2(32, 32);
    s.TouchExtraPadding      = ImVec2(0, 0);
    s.ColumnsMinSpacing      = 6.0f;
    s.LogSliderDeadzone      = 4.0f;
    s.TabBarOverlineSize     = 1.0f;
    s.SeparatorTextPadding   = ImVec2(20.0f, 3.0f);
    s.DockingSeparatorSize   = 2.0f;
    s.DisplayWindowPadding   = ImVec2(19, 19);
    s.DisplaySafeAreaPadding = ImVec2(3, 3);
    s.MouseCursorScale       = 1.0f;

    // --- Colors.
    auto V = [](ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); };
    auto L = [&](ImU32 c, float amt) { return V(Lift(c, amt, d)); };
    auto A = [&](ImU32 c, float alpha) { ImVec4 v = V(c); v.w = alpha; return v; };

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]                   = V(t.text);
    c[ImGuiCol_TextDisabled]           = V(t.textFaint);
    c[ImGuiCol_TextSelectedBg]         = V(t.selection);
    c[ImGuiCol_TextLink]               = V(t.link);

    c[ImGuiCol_WindowBg]               = V(t.background);
    c[ImGuiCol_ChildBg]                = V(t.surface);
    c[ImGuiCol_PopupBg]                = V(t.surfacePopup);
    c[ImGuiCol_Border]                 = V(t.border);
    c[ImGuiCol_BorderShadow]           = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_FrameBg]                = V(t.surfaceInput);
    c[ImGuiCol_FrameBgHovered]         = L(t.surfaceInput, 0.08f);
    c[ImGuiCol_FrameBgActive]          = L(t.surfaceInput, 0.14f);

    c[ImGuiCol_TitleBg]                = V(t.background);
    c[ImGuiCol_TitleBgActive]          = V(t.surface);
    c[ImGuiCol_TitleBgCollapsed]       = V(t.background);
    c[ImGuiCol_MenuBarBg]              = V(t.surface);

    c[ImGuiCol_ScrollbarBg]            = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]          = L(t.surfaceInput, 0.10f);
    c[ImGuiCol_ScrollbarGrabHovered]   = L(t.surfaceInput, 0.20f);
    c[ImGuiCol_ScrollbarGrabActive]    = L(t.surfaceInput, 0.30f);

    c[ImGuiCol_CheckMark]              = V(t.accent);
    c[ImGuiCol_SliderGrab]             = V(t.accent);
    c[ImGuiCol_SliderGrabActive]       = L(t.accent, 0.20f);

    c[ImGuiCol_Button]                 = V(t.surfaceInput);
    c[ImGuiCol_ButtonHovered]          = L(t.surfaceInput, 0.12f);
    c[ImGuiCol_ButtonActive]           = L(t.surfaceInput, 0.20f);

    c[ImGuiCol_Header]                 = V(t.surfaceInput);
    c[ImGuiCol_HeaderHovered]          = L(t.surfaceInput, 0.12f);
    c[ImGuiCol_HeaderActive]           = L(t.surfaceInput, 0.20f);

    c[ImGuiCol_Separator]              = V(t.border);
    c[ImGuiCol_SeparatorHovered]       = V(t.accent);
    c[ImGuiCol_SeparatorActive]        = V(t.accent);

    c[ImGuiCol_ResizeGrip]             = A(t.border, 0.60f);
    c[ImGuiCol_ResizeGripHovered]      = V(t.accent);
    c[ImGuiCol_ResizeGripActive]       = V(t.accent);

    c[ImGuiCol_Tab]                    = V(t.background);
    c[ImGuiCol_TabHovered]             = L(t.surfaceInput, 0.14f);
    c[ImGuiCol_TabSelected]            = V(t.surfaceInput);
    c[ImGuiCol_TabSelectedOverline]    = V(t.accent);
    c[ImGuiCol_TabDimmed]              = V(t.background);
    c[ImGuiCol_TabDimmedSelected]      = V(t.surface);
    c[ImGuiCol_TabDimmedSelectedOverline] = A(t.accent, 0.0f);

    c[ImGuiCol_DockingPreview]         = A(t.accent, 0.55f);
    c[ImGuiCol_DockingEmptyBg]         = V(t.background);

    c[ImGuiCol_PlotLines]              = V(t.textMuted);
    c[ImGuiCol_PlotLinesHovered]       = V(t.accent);
    c[ImGuiCol_PlotHistogram]          = V(t.warning);
    c[ImGuiCol_PlotHistogramHovered]   = L(t.warning, 0.20f);

    c[ImGuiCol_TableHeaderBg]          = V(t.surfaceInput);
    c[ImGuiCol_TableBorderStrong]      = V(t.border);
    c[ImGuiCol_TableBorderLight]       = A(t.border, 0.55f);
    c[ImGuiCol_TableRowBg]             = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]          = d ? ImVec4(1, 1, 1, 0.035f)
                                           : ImVec4(0, 0, 0, 0.025f);

    c[ImGuiCol_DragDropTarget]         = V(t.accent);
    c[ImGuiCol_NavCursor]              = V(t.accent);
    c[ImGuiCol_NavWindowingHighlight]  = A(t.text, 0.70f);
    c[ImGuiCol_NavWindowingDimBg]      = d ? ImVec4(0.20f, 0.20f, 0.20f, 0.35f)
                                           : ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
    c[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.04f, 0.04f, 0.05f, 0.65f);

    // A translucent window or popup background renders incorrectly once a
    // panel is dragged out into its own platform window, so force them opaque
    // here rather than relying on every theme author remembering.
    c[ImGuiCol_WindowBg].w = 1.0f;
    c[ImGuiCol_PopupBg].w  = 1.0f;

    if (sizeScale != 1.0f)
        s.ScaleAllSizes(sizeScale);

    // Text scales by the same factor. Since 1.92 the atlas rasterises glyphs
    // on demand, so this is all it takes -- no atlas rebuild, no font reload.
    s.FontScaleMain = sizeScale;
}
}
