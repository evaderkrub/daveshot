#pragma once

#include <string>

// Starting daveshot when the user signs in, so the hotkeys work from the
// first press without anybody having to remember to launch it.
//
// This is a registration with the OS -- a Run value in the registry on
// Windows, a .desktop file in ~/.config/autostart on Linux -- and it names
// this executable by its full path. That is deliberate for a portable
// build: the entry follows whichever copy the user ticked the box in, and
// a copy that has been moved shows as "off" rather than as a dangling
// entry, because the answer is read back by comparing paths.
//
// Nothing here is a value in the settings file, and nothing happens on its
// own: the settings panel offers a checkbox, and the loop applies it.
namespace daveshot::autostart
{
    enum class State
    {
        Unsupported,   // no place to register on this desktop -- offer nothing
        Off,
        On,            // this executable is registered
    };

    // Reads the registry or the autostart folder, so cheap enough to call
    // when something changes and not something to call every frame. The
    // loop caches the answer on AppState.
    State Query();

    // Registers this executable, to be started with --background so it
    // arrives in the tray rather than on top of whatever the user was
    // about to do. Disable removes exactly that registration.
    bool Enable(std::string& error);
    bool Disable(std::string& error);

    // What the checkbox says: "Start with Windows", "Start at login".
    const char* Label();
}
