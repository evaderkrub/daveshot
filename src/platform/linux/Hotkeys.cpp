#include "platform/Hotkeys.h"

#include "platform/linux/HotkeyBackends.h"
#include "platform/linux/Session.h"

#include <cctype>
#include <cstdlib>

namespace daveshot::hotkeys
{
namespace
{
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

    bool KeyFromName(const std::string& name, std::string& key)
    {
        if (name.size() == 1)
        {
            const char c = name[0];
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            {
                key = name;
                return true;
            }
        }
        if (name == "printscreen" || name == "prtsc" || name == "print")
        {
            key = "print";
            return true;
        }
        if (name == "insert" || name == "home" || name == "space")
        {
            key = name;
            return true;
        }
        if (name.size() >= 2 && name[0] == 'f')
        {
            const int number = std::atoi(name.c_str() + 1);
            if (number >= 1 && number <= 24)
            {
                key = "f" + std::to_string(number);
                return true;
            }
        }
        return false;
    }

    bool UsePortal()
    {
        return session::IsWayland();
    }
}

bool ParseCombo(const std::string& text, KeyCombo& out)
{
    const std::string normalised = Normalise(text);
    if (normalised.empty() || normalised == "none")
    {
        out = KeyCombo{};
        out.none = true;
        return true;
    }

    KeyCombo combo;
    size_t start  = 0;
    bool   gotKey = false;

    while (start <= normalised.size())
    {
        size_t plus = normalised.find('+', start);
        if (plus == std::string::npos)
            plus = normalised.size();

        const std::string token = normalised.substr(start, plus - start);
        start = plus + 1;

        if (!token.empty())
        {
            if      (token == "ctrl" || token == "control") combo.ctrl  = true;
            else if (token == "shift")                      combo.shift = true;
            else if (token == "alt")                        combo.alt   = true;
            else if (token == "win" || token == "super")    combo.super = true;
            else
            {
                if (gotKey || !KeyFromName(token, combo.key))
                    return false;
                gotKey = true;
            }
        }

        if (plus == normalised.size())
            break;
    }

    if (!gotKey)
        return false;
    out = combo;
    return true;
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
        "Super+Shift+S",
    };
    return presets;
}

bool IsParseable(const std::string& name)
{
    KeyCombo combo;
    return ParseCombo(name, combo);
}

void Install()
{
    // Both backends open their connection on first use; there is no hook
    // to install the way there is on Windows.
}

void Shutdown()
{
    if (UsePortal())
        portalhotkeys::Shutdown();
    else
        x11hotkeys::Shutdown();
}

void UnregisterAll()
{
    if (UsePortal())
        portalhotkeys::UnregisterAll();
    else
        x11hotkeys::UnregisterAll();
}

bool Register(const std::string& regionCombo, const std::string& screenCombo,
              std::string& error)
{
    struct Entry { Action action; const std::string* text; const char* what; };
    const Entry entries[] = {
        { Action_Region, &regionCombo, "the region hotkey" },
        { Action_Screen, &screenCombo, "the full-screen hotkey" },
    };

    std::vector<Binding> bindings;
    std::string failures;
    for (const Entry& entry : entries)
    {
        KeyCombo combo;
        if (!ParseCombo(*entry.text, combo))
        {
            if (!failures.empty()) failures += "; ";
            failures += std::string(entry.what) + " (" + *entry.text +
                        ") is not a combination we understand";
            continue;
        }
        if (combo.none)
            continue;
        bindings.push_back(Binding{ entry.action, combo, entry.what });
    }

    std::string backendError;
    const bool ok = UsePortal() ? portalhotkeys::Register(bindings, backendError)
                                : x11hotkeys::Register(bindings, backendError);
    if (!ok)
    {
        if (!failures.empty()) failures += "; ";
        failures += backendError;
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
    if (UsePortal())
        portalhotkeys::Drain(out);
    else
        x11hotkeys::Drain(out);
}

std::string TakeError()
{
    return UsePortal() ? portalhotkeys::TakeError() : std::string();
}
}
