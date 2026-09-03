#include "platform/Autostart.h"

#include "platform/Paths.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace daveshot::autostart
{
namespace
{
    // The XDG autostart folder: every desktop that follows the spec --
    // GNOME, KDE, XFCE, the rest -- starts what it finds here at login.
    std::string Folder()
    {
        const char* configHome = std::getenv("XDG_CONFIG_HOME");
        if (configHome != nullptr && *configHome != '\0')
            return std::string(configHome) + "/autostart";
        const char* home = std::getenv("HOME");
        if (home == nullptr || *home == '\0')
            return std::string();
        return std::string(home) + "/.config/autostart";
    }

    std::string EntryPath()
    {
        const std::string folder = Folder();
        return folder.empty() ? std::string() : folder + "/org.daveshot.daveshot.desktop";
    }

    // An Exec value is split on spaces, so the path is quoted, and inside
    // the quotes the spec wants backslash and the quote itself escaped.
    std::string QuotedForExec(const std::string& path)
    {
        std::string out = "\"";
        for (char c : path)
        {
            if (c == '"' || c == '\\' || c == '$' || c == '`')
                out += '\\';
            out += c;
        }
        out += "\"";
        return out;
    }

    // Written in full rather than from the template under assets/linux:
    // that one is the application entry, and an autostart entry starts the
    // program in the background.
    std::string EntryText()
    {
        return "[Desktop Entry]\n"
               "Type=Application\n"
               "Name=daveshot\n"
               "Comment=Screen capture tool\n"
               "Exec=" + QuotedForExec(paths::ExePath()) + " --background\n"
               "Icon=" + paths::Asset("icons/daveshot.png") + "\n"
               "Terminal=false\n"
               "StartupWMClass=daveshot\n"
               "X-GNOME-Autostart-enabled=true\n";
    }

    bool ReadEntry(std::string& text)
    {
        std::FILE* f = paths::OpenFile(EntryPath(), "rb");
        if (f == nullptr)
            return false;
        char chunk[512];
        size_t read = 0;
        while ((read = std::fread(chunk, 1, sizeof(chunk), f)) > 0)
            text.append(chunk, read);
        std::fclose(f);
        return true;
    }
}

State Query()
{
    if (EntryPath().empty())
        return State::Unsupported;

    // An entry that names some other copy of daveshot is not this one
    // starting at login: say "off", and ticking the box points it here.
    std::string text;
    if (!ReadEntry(text))
        return State::Off;
    return text.find(paths::ExePath()) != std::string::npos ? State::On : State::Off;
}

bool Enable(std::string& error)
{
    const std::string folder = Folder();
    if (folder.empty())
    {
        error = "there is no home folder to put an autostart entry in";
        return false;
    }
    if (!paths::EnsureFolder(folder, error))
        return false;

    const std::string path = EntryPath();
    std::FILE* f = paths::OpenFile(path, "wb");
    if (f == nullptr)
    {
        error = "could not write " + path;
        return false;
    }
    const std::string text = EntryText();
    const bool written = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    std::fclose(f);
    if (!written)
    {
        error = "could not write " + path;
        return false;
    }
    return true;
}

bool Disable(std::string& error)
{
    const std::string path = EntryPath();
    if (path.empty() || !paths::Exists(path))
        return true;
    if (!paths::RemoveFile(path))
    {
        error = "could not remove " + path;
        return false;
    }
    return true;
}

const char* Label()
{
    return "Start at login";
}
}
