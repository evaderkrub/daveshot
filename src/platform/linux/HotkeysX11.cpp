#include "platform/linux/HotkeyBackends.h"

#include "platform/linux/X11.h"

#include <X11/keysym.h>

#include <cctype>

namespace daveshot::x11hotkeys
{
namespace
{
    struct Grab
    {
        hotkeys::Action action;
        KeyCode         keycode;
        unsigned        modifiers;   // without the lock bits
    };

    std::vector<Grab>            gGrabs;
    std::vector<hotkeys::Action> gPending;

    // Caps Lock and Num Lock are modifier bits to X, so a grab on Ctrl+S
    // alone would go quiet the moment Num Lock came on. Grabbing all four
    // lock combinations is the conventional answer.
    constexpr unsigned kLockMasks[] = { 0, LockMask, Mod2Mask, LockMask | Mod2Mask };
    constexpr unsigned kRealModifiers = ControlMask | ShiftMask | Mod1Mask | Mod4Mask;

    unsigned ModifiersOf(const hotkeys::KeyCombo& combo)
    {
        unsigned mods = 0;
        if (combo.ctrl)  mods |= ControlMask;
        if (combo.shift) mods |= ShiftMask;
        if (combo.alt)   mods |= Mod1Mask;
        if (combo.super) mods |= Mod4Mask;
        return mods;
    }

    KeySym KeysymOf(const std::string& key)
    {
        if (key == "print") return XK_Print;
        if (key == "insert") return XK_Insert;
        if (key == "home")   return XK_Home;
        if (key == "space")  return XK_space;
        if (key.size() >= 2 && key[0] == 'f')
        {
            // XStringToKeysym wants "F9", not "f9".
            std::string name = key;
            name[0] = 'F';
            return XStringToKeysym(name.c_str());
        }
        return XStringToKeysym(key.c_str());   // "a", "4"
    }
}

bool Register(const std::vector<hotkeys::Binding>& bindings, std::string& error)
{
    UnregisterAll();

    Display* display = x11::Get();
    if (display == nullptr)
    {
        error = "no X display is available for global hotkeys";
        return false;
    }
    const Window root = DefaultRootWindow(display);

    std::string failures;
    for (const hotkeys::Binding& binding : bindings)
    {
        const KeySym  keysym  = KeysymOf(binding.combo.key);
        const KeyCode keycode = keysym ? XKeysymToKeycode(display, keysym) : 0;
        if (keycode == 0)
        {
            if (!failures.empty()) failures += "; ";
            failures += std::string(binding.what) + " uses a key this keyboard does not have";
            continue;
        }

        const unsigned modifiers = ModifiersOf(binding.combo);
        bool taken = false;
        for (unsigned lock : kLockMasks)
        {
            // Another client holding the same grab answers with BadAccess;
            // the trap turns that into a false return instead of an exit.
            x11::ErrorTrap trap(display);
            XGrabKey(display, keycode, modifiers | lock, root, False,
                     GrabModeAsync, GrabModeAsync);
            if (trap.Failed())
                taken = true;
        }

        if (taken)
        {
            for (unsigned lock : kLockMasks)
                XUngrabKey(display, keycode, modifiers | lock, root);
            if (!failures.empty()) failures += "; ";
            failures += std::string(binding.what) + " is already taken by another application";
            continue;
        }
        gGrabs.push_back(Grab{ binding.action, keycode, modifiers });
    }
    XFlush(display);

    if (!failures.empty())
    {
        error = failures;
        return false;
    }
    return true;
}

void UnregisterAll()
{
    Display* display = x11::Get();
    if (display != nullptr)
    {
        const Window root = DefaultRootWindow(display);
        for (const Grab& grab : gGrabs)
            for (unsigned lock : kLockMasks)
                XUngrabKey(display, grab.keycode, grab.modifiers | lock, root);
        XFlush(display);
    }
    gGrabs.clear();
}

void Drain(std::vector<hotkeys::Action>& out)
{
    out.clear();
    Display* display = x11::Get();
    if (display == nullptr)
        return;

    // Grabbed keys arrive on our own connection, so nothing else drains
    // them. Only key presses are of interest; a stray event of another kind
    // on this connection is simply dropped.
    while (XPending(display) > 0)
    {
        XEvent event;
        XNextEvent(display, &event);
        if (event.type != KeyPress)
            continue;

        const unsigned state = event.xkey.state & kRealModifiers;
        for (const Grab& grab : gGrabs)
        {
            if (grab.keycode == event.xkey.keycode && grab.modifiers == state)
            {
                gPending.push_back(grab.action);
                break;
            }
        }
    }

    out.swap(gPending);
    gPending.clear();
}

void Shutdown()
{
    UnregisterAll();
    gPending.clear();
}
}
