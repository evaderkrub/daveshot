#include "platform/Hotkeys.h"

#include <SDL3/SDL.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace daveshot::hotkeys
{
namespace
{
    struct Combo
    {
        UINT modifiers = 0;
        UINT key       = 0;
        bool none      = false;   // "None": parses, registers nothing
    };

    std::vector<Action> gPending;
    std::vector<int>    gRegistered;
    bool                gHookInstalled = false;

    std::string Normalise(const std::string& text)
    {
        std::string out;
        for (char c : text)
        {
            if (c == ' ' || c == '\t')
                continue;
            out += (char)std::tolower((unsigned char)c);
        }
        return out;
    }

    bool KeyFromName(const std::string& name, UINT& key)
    {
        if (name.size() == 1)
        {
            const char c = name[0];
            if (c >= 'a' && c <= 'z') { key = (UINT)('A' + (c - 'a')); return true; }
            if (c >= '0' && c <= '9') { key = (UINT)c; return true; }
        }

        if (name == "printscreen" || name == "prtsc" || name == "print")
        {
            key = VK_SNAPSHOT;
            return true;
        }
        if (name == "insert") { key = VK_INSERT; return true; }
        if (name == "home")   { key = VK_HOME;   return true; }
        if (name == "space")  { key = VK_SPACE;  return true; }

        if (name.size() >= 2 && name[0] == 'f')
        {
            const int number = std::atoi(name.c_str() + 1);
            if (number >= 1 && number <= 24)
            {
                key = (UINT)(VK_F1 + number - 1);
                return true;
            }
        }
        return false;
    }

    bool Parse(const std::string& text, Combo& out)
    {
        const std::string normalised = Normalise(text);
        if (normalised.empty() || normalised == "none")
        {
            out = Combo{};
            out.none = true;
            return true;
        }

        Combo combo;
        size_t start = 0;
        bool   gotKey = false;

        while (start <= normalised.size())
        {
            size_t plus = normalised.find('+', start);
            if (plus == std::string::npos)
                plus = normalised.size();

            const std::string token = normalised.substr(start, plus - start);
            start = plus + 1;

            if (token.empty())
                continue;

            if      (token == "ctrl" || token == "control") combo.modifiers |= MOD_CONTROL;
            else if (token == "shift")                      combo.modifiers |= MOD_SHIFT;
            else if (token == "alt")                        combo.modifiers |= MOD_ALT;
            else if (token == "win" || token == "super")    combo.modifiers |= MOD_WIN;
            else
            {
                if (gotKey || !KeyFromName(token, combo.key))
                    return false;
                gotKey = true;
            }

            if (plus == normalised.size())
                break;
        }

        if (!gotKey)
            return false;

        // Holding the key down should not fire a burst of captures.
        combo.modifiers |= MOD_NOREPEAT;
        out = combo;
        return true;
    }

    // SDL pumps the thread's message queue for us; this hook sees every
    // message it takes off, WM_HOTKEY included. Registering with a null
    // window posts hotkeys to the thread rather than to a window, which is
    // why they arrive here at all.
    bool SDLCALL MessageHook(void*, MSG* msg)
    {
        if (msg != nullptr && msg->message == WM_HOTKEY)
        {
            const int id = (int)msg->wParam;
            if (id == Action_Region || id == Action_Screen)
                gPending.push_back((Action)id);
        }
        return true;   // let SDL carry on with the message
    }
}

const std::vector<std::string>& Presets()
{
    static const std::vector<std::string> presets = {
        "None",
        "PrintScreen",
        "Alt+PrintScreen",
        "Ctrl+PrintScreen",
        "Ctrl+Shift+S",
        "Ctrl+Shift+A",
        "Ctrl+Shift+4",
        "Ctrl+Alt+S",
    };
    return presets;
}

bool IsParseable(const std::string& name)
{
    Combo combo;
    return Parse(name, combo);
}

void Install()
{
    if (gHookInstalled)
        return;
    SDL_SetWindowsMessageHook(MessageHook, nullptr);
    gHookInstalled = true;
}

void Shutdown()
{
    UnregisterAll();
    if (gHookInstalled)
    {
        SDL_SetWindowsMessageHook(nullptr, nullptr);
        gHookInstalled = false;
    }
    gPending.clear();
}

void UnregisterAll()
{
    for (int id : gRegistered)
        UnregisterHotKey(nullptr, id);
    gRegistered.clear();
}

bool Register(const std::string& regionCombo, const std::string& screenCombo,
              std::string& error)
{
    UnregisterAll();

    struct Entry { Action action; const std::string* text; const char* what; };
    const Entry entries[] = {
        { Action_Region, &regionCombo, "the region hotkey" },
        { Action_Screen, &screenCombo, "the full-screen hotkey" },
    };

    std::string failures;
    for (const Entry& entry : entries)
    {
        Combo combo;
        if (!Parse(*entry.text, combo))
        {
            if (!failures.empty()) failures += "; ";
            failures += std::string(entry.what) + " (" + *entry.text + ") is not a combination we understand";
            continue;
        }
        if (combo.none)
            continue;

        if (RegisterHotKey(nullptr, (int)entry.action, combo.modifiers, combo.key) == 0)
        {
            if (!failures.empty()) failures += "; ";
            failures += std::string(entry.what) + " (" + *entry.text +
                        ") is already taken by another application";
            continue;
        }
        gRegistered.push_back((int)entry.action);
    }

    if (!failures.empty())
    {
        error = failures;
        return false;
    }
    return true;
}

void Drain(std::vector<Action>& out)
{
    out = gPending;
    gPending.clear();
}

std::string TakeError()
{
    return std::string();   // RegisterHotKey answers on the spot
}
}
