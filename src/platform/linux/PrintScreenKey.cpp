#include "platform/PrintScreenKey.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace daveshot::printkey
{
namespace
{
    // Where GNOME keeps its own Print binding. It has moved once: the shell
    // took screenshots over in GNOME 42, and before that the key belonged to
    // the settings daemon. Both are checked, because a desktop that still
    // holds Print in the old place holds it just as firmly.
    struct Binding
    {
        const char* schema;
        const char* key;
    };
    const Binding kBindings[] = {
        { "org.gnome.shell.keybindings",                "show-screenshot-ui" },
        { "org.gnome.settings-daemon.plugins.media-keys", "screenshot"       },
    };

    bool IsGnome()
    {
        const char* desktop = std::getenv("XDG_CURRENT_DESKTOP");
        if (desktop == nullptr)
            return false;
        std::string lower;
        for (const char* c = desktop; *c != '\0'; ++c)
            lower += (char)std::tolower((unsigned char)*c);
        return lower.find("gnome") != std::string::npos;
    }

    // Runs one of the fixed commands below and collects its output. Nothing
    // the user typed reaches here -- every command is built from the table
    // above -- so there is no shell quoting to get wrong.
    bool Run(const std::string& command, std::string& output)
    {
        FILE* pipe = popen((command + " 2>/dev/null").c_str(), "r");
        if (pipe == nullptr)
            return false;

        char buffer[256];
        while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr)
            output += buffer;

        return pclose(pipe) == 0;
    }

    bool Run(const std::string& command)
    {
        std::string ignored;
        return Run(command, ignored);
    }

    std::string Target(const Binding& binding)
    {
        return std::string(binding.schema) + " " + binding.key;
    }

    // gsettings prints a GVariant: "@as []" for nothing, "['Print']" for the
    // bare key, "['<Alt>Print']" for a combination that leaves Print alone --
    // which is why this looks for the quoted whole word rather than "Print".
    bool HoldsPrint(const Binding& binding, bool& known)
    {
        std::string output;
        known = Run("gsettings get " + Target(binding), output);
        if (!known)
            return false;
        return output.find("'Print'") != std::string::npos;
    }
}

Status Query()
{
    Status status;
    if (!IsGnome())
        return status;   // Unknown: another desktop's binding is not ours to read

    bool anyKnown = false;
    bool held     = false;
    for (const Binding& binding : kBindings)
    {
        bool known = false;
        if (HoldsPrint(binding, known))
            held = true;
        anyKnown = anyKnown || known;
    }

    if (!anyKnown)
        return status;   // no gsettings, or a GNOME without those schemas

    if (held)
    {
        status.state = State::Desktop;
        status.hint  = "GNOME opens its own screenshot UI when you press Print Screen, "
                       "and will not hand the key to daveshot until it stops.";
    }
    else
    {
        status.state = State::Ours;
        status.hint  = "Print Screen is daveshot's. GNOME's own screenshot UI is off "
                       "the key; Shift+Print and Alt+Print are still GNOME's.";
    }
    return status;
}

bool Take(std::string& error)
{
    bool changed = false;
    for (const Binding& binding : kBindings)
    {
        bool known = false;
        if (!HoldsPrint(binding, known))
            continue;
        if (!Run("gsettings set " + Target(binding) + " \"[]\""))
        {
            error = "gsettings would not clear GNOME's " + std::string(binding.key) +
                    " shortcut";
            return false;
        }
        changed = true;
    }

    if (!changed)
    {
        error = "GNOME does not appear to hold Print Screen";
        return false;
    }
    return true;
}

bool GiveBack(std::string& error)
{
    // Reset rather than write 'Print' back: the distribution's default is
    // what the user had before we touched it.
    bool restored = false;
    for (const Binding& binding : kBindings)
    {
        // A GNOME that never had this schema -- the key moved once -- has
        // nothing to restore, and resetting it would only fail.
        bool known = false;
        (void)HoldsPrint(binding, known);
        if (!known)
            continue;

        if (!Run("gsettings reset " + Target(binding)))
        {
            error = "gsettings would not restore GNOME's " + std::string(binding.key) +
                    " shortcut";
            return false;
        }
        restored = true;
    }

    if (!restored)
    {
        error = "gsettings could not be reached";
        return false;
    }
    return true;
}
}
