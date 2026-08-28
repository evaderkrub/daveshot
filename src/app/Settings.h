#pragma once

#include <string>

namespace daveshot
{
    enum class ImageFormat
    {
        Png,
        Jpeg,
    };

    // Everything the application remembers between runs. Plain data: no ImGui
    // types, no file handles, so the parsing rules below can be tested without
    // a window on screen.
    struct Settings
    {
        // --- Appearance ---------------------------------------------------
        // Multiplies both the font size and the ImGui style metrics. 1.0 is
        // the design size; the OS DPI scale is applied on top of this.
        float       uiScale = 1.0f;
        std::string theme   = "Wili Dark";

        // --- Where captures go ---------------------------------------------
        // Empty means "the Pictures\daveshot folder", resolved at startup --
        // storing the resolved path would bake one machine's user profile
        // into a settings file meant to travel with the folder.
        std::string saveFolder;
        std::string filenamePattern = "daveshot_%Y-%m-%d_%H-%M-%S";
        ImageFormat format          = ImageFormat::Png;
        int         jpegQuality     = 90;

        // --- What happens on capture ---------------------------------------
        bool autoSave      = true;
        bool autoCopy      = true;
        int  delaySeconds  = 0;
        bool hideOnCapture = true;

        // --- Global hotkey -------------------------------------------------
        // A name from the fixed list in Hotkeys.h rather than a raw virtual
        // key: the interface offers a combo, and an unparseable value from a
        // hand-edited file falls back to the default instead of registering
        // something nobody can press.
        bool        hotkeyEnabled = true;
        std::string hotkeyRegion  = "Ctrl+Shift+S";
        std::string hotkeyScreen  = "PrintScreen";

        // --- History -------------------------------------------------------
        int historyLimit = 12;

        static constexpr float kMinScale = 0.75f;
        static constexpr float kMaxScale = 3.0f;
        static constexpr int   kMaxDelaySeconds = 60;
        static constexpr int   kMinHistory = 1;
        static constexpr int   kMaxHistory = 50;
    };

    // Clamped rather than rejected: a settings file edited by hand or written
    // by an older build should still start the application.
    float ClampUiScale(float scale);
    int   ClampDelaySeconds(int seconds);
    int   ClampJpegQuality(int quality);
    int   ClampHistoryLimit(int limit);

    const char* FormatKey(ImageFormat format);
    ImageFormat FormatFromKey(const std::string& key);

    // key=value text, one per line, '#' comments. Unknown keys are ignored so
    // a newer build's file does not break an older one.
    bool ParseSettings(const std::string& text, Settings& out);
    std::string SerializeSettings(const Settings& settings);

    // A missing file is not a failure -- it means "first run", and defaults
    // apply. Only an unreadable or unwritable file sets error.
    bool LoadSettings(const std::string& path, Settings& out, std::string& error);
    bool SaveSettings(const std::string& path, const Settings& settings, std::string& error);
}
