#pragma once

#include "imgui.h"

// Token-based theming, carried over from fwcom.
//
// A theme is 17 semantic colors. Everything else -- the ~50 ImGui style
// colors and the style metrics -- is derived from them, so adding a theme
// means picking 17 colors rather than maintaining a 50-entry list by hand and
// hoping nothing was missed.
namespace daveshot::theme
{
    struct Tokens
    {
        const char* name;
        bool        isDark;        // chooses the ImGui base style to layer over

        ImU32 background;          // window bg
        ImU32 surface;             // panels, child regions
        ImU32 surfaceInput;        // text fields, buttons
        ImU32 surfacePopup;        // menus, combos, tooltips -- MUST be alpha 255
        ImU32 border;

        ImU32 text;
        ImU32 textMuted;
        ImU32 textFaint;

        ImU32 accent;
        ImU32 link;

        ImU32 codeBackground;
        ImU32 inlineCodeBackground;
        ImU32 inlineCodeText;

        ImU32 selection;           // carries its own alpha; drawn over content

        ImU32 success;
        ImU32 warning;
        ImU32 danger;
    };

    // Colors the widget helpers draw with. Rebuilt whenever a theme is applied.
    struct Palette
    {
        ImU32 text, textMuted, textFaint;
        ImU32 heading;
        ImU32 rule, quoteBar;
        ImU32 link;

        ImU32 surface, surfaceHovered, surfaceActive;
        ImU32 border, borderHovered;

        ImU32 codeBackground;
        ImU32 inlineCodeBackground, inlineCodeText;

        ImU32 accent, accentHovered, accentActive, accentText;

        ImU32 success, warning, danger, neutral;

        // Pill/status fills: the state color at low alpha, for filled backgrounds.
        ImU32 successFill, warningFill, dangerFill, neutralFill, accentFill;
    };

    int           Count();
    const Tokens& At(int index);            // clamps to range
    int           IndexByName(const char* name);   // 0 if unknown

    const Palette& Current();
    Palette        Build(const Tokens& tokens);

    // Blend toward white on dark themes, toward black on light ones, so one
    // "slightly brighter on hover" rule reads correctly in both directions.
    ImU32 Lift(ImU32 color, float amount, bool isDark);
    ImU32 WithAlpha(ImU32 color, int alpha);

    // WCAG relative-luminance contrast ratio, 1.0 .. 21.0. Alpha is ignored.
    float ContrastRatio(ImU32 a, ImU32 b);

    // Rebuilds the entire ImGui style -- colors AND metrics -- from the
    // theme's tokens, then scales sizes and the font by sizeScale.
    //
    // Sizes are reset from constants on every call, so this is idempotent.
    // ScaleAllSizes multiplies the current metrics and is NOT idempotent on
    // its own; that is why the reset is mandatory rather than tidy.
    //
    // Call between frames only: it replaces the whole style, and applying it
    // mid-frame gives inconsistent metrics for the rest of that frame.
    void Apply(int index, float sizeScale);

    int CurrentIndex();

    // Number of times Apply() has actually run, process-lifetime. Exists so
    // tests can prove a rebuild did (or did not) happen -- a style memcmp
    // alone cannot tell "no-op" from "re-applied the same theme", since Apply
    // is deterministic and produces byte-identical output either way.
    unsigned ApplyCountForTests();
}
