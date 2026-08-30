#pragma once

#include <string>

// The Print Screen key, and who answers it.
//
// Every desktop ships a screenshot tool of its own bound to Print, and that
// binding sits above ours: Windows 11 opens the Snipping Tool, GNOME opens
// its screenshot UI. Registering the hotkey is not enough to get the key --
// on Windows both fire, and on GNOME the shell refuses to hand it over at
// all -- so taking Print over means turning the desktop's own binding off.
//
// That is a change to the user's machine rather than to ours, so nothing
// here happens on its own: the settings panel says what it found and offers
// a button, and the change goes both ways.
namespace daveshot::printkey
{
    enum class State
    {
        Unknown,   // no binding we know how to read here -- offer nothing
        Desktop,   // the desktop's own screenshot tool answers Print
        Ours,      // nothing else answers it; the hotkey is ours to keep
    };

    struct Status
    {
        State       state = State::Unknown;
        // What to tell the user, written by the platform that knows which
        // tool it is talking about. One line, empty when state is Unknown.
        std::string hint;
    };

    // Reads the registry on Windows and runs gsettings on Linux, so it is
    // cheap enough to call when something changes and too expensive to call
    // every frame. The loop caches the answer on AppState.
    Status Query();

    // Desktop -> Ours, and back again. Both leave everything alone and
    // report a message the interface can show when they cannot do it.
    bool Take(std::string& error);
    bool GiveBack(std::string& error);
}
