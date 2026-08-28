#pragma once

#include <string>

namespace daveshot
{
    // Everything the application remembers between runs. Plain data: no ImGui
    // types, no file handles, so the parsing rules below can be tested without
    // a window on screen.
    struct Settings
    {
        // Multiplies both the font size and the ImGui style metrics. 1.0 is
        // the design size; the OS DPI scale is applied on top of this.
        float       uiScale  = 1.0f;
        std::string theme    = "Wili Dark";

        static constexpr float kMinScale = 0.75f;
        static constexpr float kMaxScale = 3.0f;
    };

    // Scale is clamped rather than rejected: a settings file edited by hand or
    // written by an older build should still start the application.
    float ClampUiScale(float scale);

    // key=value text, one per line, '#' comments. Unknown keys are ignored so
    // a newer build's file does not break an older one.
    bool ParseSettings(const std::string& text, Settings& out);
    std::string SerializeSettings(const Settings& settings);

    // A missing file is not a failure -- it means "first run", and defaults
    // apply. Only an unreadable or unwritable file sets error.
    bool LoadSettings(const std::string& path, Settings& out, std::string& error);
    bool SaveSettings(const std::string& path, const Settings& settings, std::string& error);
}
