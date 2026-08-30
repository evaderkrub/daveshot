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
        //
        // PrintScreen is the key people reach for, so it is a default on
        // both platforms -- for the full screen on Windows, and for a region
        // on Linux, which is the capture worth spending the good key on.
        //
        // It comes with a condition on Linux: GNOME holds Print for its own
        // screenshot UI, and the portal will not hand over a key the shell
        // has. Until it is freed the desktop's dialog is where the user picks
        // something else --
        //
        //     gsettings set org.gnome.shell.keybindings show-screenshot-ui "[]"
        //
        // -- and because the shortcuts are approved as one set (see
        // HotkeysPortal.cpp), a Print that is refused takes the other hotkey
        // with it rather than failing on its own.
        bool        hotkeyEnabled = true;
#ifdef __linux__
        std::string hotkeyRegion  = "PrintScreen";
        std::string hotkeyScreen  = "Ctrl+Alt+S";
#else
        std::string hotkeyRegion  = "Ctrl+Shift+S";
        std::string hotkeyScreen  = "PrintScreen";
#endif

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
