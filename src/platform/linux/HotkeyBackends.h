#pragma once

#include "platform/Hotkeys.h"

#include <string>
#include <vector>

// The two ways to own a key combination system-wide on Linux, behind the one
// Hotkeys.h interface. Hotkeys.cpp parses the setting and picks the backend.
namespace daveshot::hotkeys
{
    // A combination with the platform taken out of it. Key names are
    // normalised to lower case: "a".."z", "0".."9", "f1".."f24", "print",
    // "insert", "home", "space".
    struct KeyCombo
    {
        bool        ctrl  = false;
        bool        shift = false;
        bool        alt   = false;
        bool        super = false;
        std::string key;
        bool        none  = false;   // "None": parses, binds nothing
    };

    bool ParseCombo(const std::string& text, KeyCombo& out);

    struct Binding
    {
        Action   action;
        KeyCombo combo;
        const char* what;    // "the region hotkey", for messages
    };
}

// XGrabKey on the root window: the X server delivers the key to us no
// matter which client has focus.
namespace daveshot::x11hotkeys
{
    bool Register(const std::vector<hotkeys::Binding>& bindings, std::string& error);
    void UnregisterAll();
    void Drain(std::vector<hotkeys::Action>& out);
    void Shutdown();
}

// org.freedesktop.portal.GlobalShortcuts: the compositor owns every key on
// Wayland, so we describe the shortcuts we would like, the desktop shows the
// user a dialog to approve or change them, and it tells us when one fires.
namespace daveshot::portalhotkeys
{
    // Starts the binding; the desktop's answer comes back through Drain,
    // and a refusal through TakeError.
    bool Register(const std::vector<hotkeys::Binding>& bindings, std::string& error);
    void UnregisterAll();
    void Drain(std::vector<hotkeys::Action>& out);
    std::string TakeError();
    void Shutdown();
}
