#pragma once

#include <string>
#include <vector>

// System-wide hotkeys, so a capture can be taken while daveshot is behind
// whatever the user actually wants a picture of.
namespace daveshot::hotkeys
{
    // What a hotkey means to the application. Registered under these ids and
    // handed back by Drain().
    enum Action
    {
        Action_None   = 0,
        Action_Region = 1,
        Action_Screen = 2,
    };

    // Combinations offered in the interface. Any string the parser accepts
    // works in the settings file; this is just the shortlist worth clicking.
    const std::vector<std::string>& Presets();

    // "Ctrl+Shift+S", "PrintScreen", "Alt+F9". Case-insensitive, spaces
    // ignored. "None" parses successfully and registers nothing.
    bool IsParseable(const std::string& name);

    // Claims the combinations from the OS. Registering replaces whatever was
    // registered before, so this is also how a changed setting is applied.
    //
    // A combination another application already owns cannot be taken -- that
    // is the OS's rule, not ours -- so this reports which ones failed rather
    // than failing as a whole, and the interface tells the user which one to
    // change.
    bool Register(const std::string& regionCombo, const std::string& screenCombo,
                  std::string& error);

    void UnregisterAll();

    // Hotkey presses since the last call, in order. Empty most frames.
    void Drain(std::vector<Action>& out);

    // Registering is not always immediate: on Wayland the desktop asks the
    // user to approve the keys, and the answer arrives frames later, through
    // Drain. A failure that arrives that way is collected here, once. Empty
    // when there is nothing to report.
    std::string TakeError();

    // Installs the hook that notices WM_HOTKEY. Call once, after SDL's video
    // subsystem is up.
    void Install();
    void Shutdown();
}
