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
        // The file's path as text, for a shot that is going into a message
        // or a command line. The clipboard holds one thing, so this takes
        // the place of the picture; and it needs a file to point at, so it
        // does nothing unless the save is automatic too.
        bool autoCopyPath  = false;
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
        // It comes with a condition: the desktop's own screenshot tool has
        // the key first -- the Snipping Tool on Windows, GNOME's screenshot
        // UI on Linux -- and taking it back is a change to the machine's
        // settings rather than to these, so it is the button the settings
        // panel offers rather than something applied on startup. See
        // platform/PrintScreenKey.h.
        //
        // On Linux it also has a knock-on: the shortcuts are approved as one
        // set (see HotkeysPortal.cpp), so a Print the shell still holds takes
        // the other hotkey down with it rather than failing on its own.
        bool        hotkeyEnabled = true;
#if defined(__APPLE__)
        std::string hotkeyRegion  = "Ctrl+Shift+S";
        std::string hotkeyScreen  = "Ctrl+Alt+S";
#elif defined(__linux__)
        std::string hotkeyRegion  = "PrintScreen";
        std::string hotkeyScreen  = "Ctrl+Alt+S";
#else
        std::string hotkeyRegion  = "Ctrl+Shift+S";
        std::string hotkeyScreen  = "PrintScreen";
#endif

        // --- Running in the background ---------------------------------------
        // Closing the window puts daveshot in the notification area rather
        // than ending it, so the hotkeys keep working. Quitting is a separate
        // action, from the tray or the File menu. Starting at login is not
        // here: that is a registration with the OS, not a value in this
        // file -- see platform/Autostart.h.
        bool closeToTray = true;
        // A capture that starts from the tray normally ends there. This
        // brings the window up with the shot instead, for people who want
        // to see what they got.
        bool showAfterCapture = false;

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
