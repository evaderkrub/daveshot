#include "platform/Paths.h"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <shlobj.h>
#else
#  include "platform/linux/Dbus.h"
#  include <limits.h>
#  include <spawn.h>
#  include <sys/stat.h>
#  include <sys/wait.h>
#  include <unistd.h>
extern char** environ;
#endif

namespace daveshot::paths
{
namespace
{
    std::string gExeDir;

#ifdef _WIN32
    std::wstring Utf8ToWide(const std::string& utf8)
    {
        if (utf8.empty())
            return std::wstring();
        const int count = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(),
                                              nullptr, 0);
        std::wstring wide((size_t)(count > 0 ? count : 0), 0);
        if (count > 0)
            MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(),
                                wide.data(), count);
        return wide;
    }
#endif

    void StripFilename(std::string& path)
    {
        const size_t cut = path.find_last_of("/\\");
        path = (cut == std::string::npos) ? std::string(".") : path.substr(0, cut);
    }

#ifdef _WIN32
    // GetModuleFileNameW truncates rather than failing when the buffer is too
    // small, so grow until it fits instead of trusting MAX_PATH -- long paths
    // are legal on modern Windows and a truncated result would silently point
    // at the wrong folder.
    bool QueryExePath(std::string& out)
    {
        std::wstring wide(MAX_PATH, L'\0');
        for (;;)
        {
            const DWORD written = GetModuleFileNameW(nullptr, wide.data(), (DWORD)wide.size());
            if (written == 0)
                return false;
            if (written < wide.size())
            {
                wide.resize(written);
                break;
            }
            wide.resize(wide.size() * 2);
        }

        const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(),
                                              nullptr, 0, nullptr, nullptr);
        if (bytes <= 0)
            return false;
        out.resize((size_t)bytes);
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(),
                            out.data(), bytes, nullptr, nullptr);
        return true;
    }
#else
    bool QueryExePath(std::string& out)
    {
        char buffer[PATH_MAX];
        const ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
        if (len <= 0)
            return false;
        buffer[len] = '\0';
        out.assign(buffer, (size_t)len);
        return true;
    }
#endif
}

bool Init(const char* argv0, std::string& error)
{
    std::string path;
    if (!QueryExePath(path))
    {
        if (argv0 == nullptr || *argv0 == '\0')
        {
            error = "could not determine the executable's location";
            return false;
        }
        path = argv0;
    }

    StripFilename(path);
    gExeDir = path;
    return true;
}

const std::string& ExeDir()
{
    return gExeDir;
}

std::string Asset(const std::string& relative)
{
    return gExeDir + "/assets/" + relative;
}

std::string Beside(const std::string& relative)
{
    return gExeDir + "/" + relative;
}

bool Exists(const std::string& path)
{
    std::FILE* f = OpenFile(path, "rb");
    if (f == nullptr)
        return false;
    std::fclose(f);
    return true;
}

std::FILE* OpenFile(const std::string& path, const char* mode)
{
#ifdef _WIN32
    // _wfopen_s rather than _wfopen: the plain form is deprecated under
    // /W4, and the checked form is what the CRT wants us to call.
    std::FILE* f = nullptr;
    if (_wfopen_s(&f, Utf8ToWide(path).c_str(), Utf8ToWide(mode).c_str()) != 0)
        return nullptr;
    return f;
#else
    return std::fopen(path.c_str(), mode);
#endif
}

bool RemoveFile(const std::string& path)
{
#ifdef _WIN32
    return _wremove(Utf8ToWide(path).c_str()) == 0;
#else
    return std::remove(path.c_str()) == 0;
#endif
}
}

namespace daveshot::paths
{
std::string PicturesFolder()
{
#ifdef _WIN32
    PWSTR    wide = nullptr;
    std::string result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &wide)))
    {
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
        if (bytes > 1)
        {
            result.resize((size_t)bytes - 1);
            WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), bytes, nullptr, nullptr);
        }
    }
    if (wide != nullptr)
        CoTaskMemFree(wide);

    // No Pictures folder at all is exotic but not impossible (a stripped
    // service account). Falling back beside the executable keeps captures
    // going somewhere the user can find.
    if (result.empty())
        return ExeDir();
    return result;
#else
    const char* home = std::getenv("HOME");
    const std::string homeDir = home ? home : "";

    // The Pictures folder is wherever xdg-user-dirs says it is. That file
    // is a shell fragment -- XDG_PICTURES_DIR="$HOME/Pictures" -- and the
    // only expansion it ever uses is $HOME, so that is the only one done.
    const char* configHome = std::getenv("XDG_CONFIG_HOME");
    const std::string configDir = (configHome && *configHome) ? configHome
                                : homeDir.empty() ? "" : homeDir + "/.config";
    if (!configDir.empty())
    {
        if (std::FILE* f = std::fopen((configDir + "/user-dirs.dirs").c_str(), "r"))
        {
            char line[1024];
            std::string found;
            while (std::fgets(line, sizeof(line), f))
            {
                std::string text(line);
                const std::string key = "XDG_PICTURES_DIR=";
                const size_t at = text.find(key);
                if (at == std::string::npos || text.find('#') < at)
                    continue;
                std::string value = text.substr(at + key.size());
                while (!value.empty() && (value.back() == '\n' || value.back() == '\r' ||
                                          value.back() == ' '))
                    value.pop_back();
                if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
                    value = value.substr(1, value.size() - 2);
                if (value.rfind("$HOME", 0) == 0)
                    value = homeDir + value.substr(5);
                found = value;
            }
            std::fclose(f);
            if (!found.empty())
                return found;
        }
    }

    if (!homeDir.empty())
        return homeDir + "/Pictures";
    return ExeDir();
#endif
}

bool EnsureFolder(const std::string& path, std::string& error)
{
    if (path.empty())
    {
        error = "no folder was given";
        return false;
    }

#ifdef _WIN32
    std::wstring wide = Utf8ToWide(path);
    // SHCreateDirectoryExW builds the whole chain and takes backslashes only.
    for (wchar_t& c : wide)
        if (c == L'/')
            c = L'\\';

    const int result = SHCreateDirectoryExW(nullptr, wide.c_str(), nullptr);
    if (result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS ||
        result == ERROR_FILE_EXISTS)
        return true;

    error = "could not create " + path;
    return false;
#else
    // mkdir -p by hand: create each prefix in turn, and let "already there"
    // pass, because the usual case is that every one of them is.
    std::string prefix;
    for (size_t i = 0; i <= path.size(); ++i)
    {
        if (i < path.size() && path[i] != '/')
            continue;
        prefix = path.substr(0, i);
        if (prefix.empty())
            continue;
        if (mkdir(prefix.c_str(), 0755) != 0 && errno != EEXIST)
        {
            error = "could not create " + path;
            return false;
        }
    }

    struct stat info{};
    if (stat(path.c_str(), &info) != 0 || !S_ISDIR(info.st_mode))
    {
        error = path + " is not a folder";
        return false;
    }
    return true;
#endif
}

bool RevealInFileBrowser(const std::string& path)
{
#ifdef _WIN32
    const std::wstring wide = Utf8ToWide(path);
    // ILCreateFromPath + SHOpenFolderAndSelectItems selects the file rather
    // than merely opening its folder, which is what "show it to me" means.
    PIDLIST_ABSOLUTE item = ILCreateFromPathW(wide.c_str());
    if (item == nullptr)
        return false;

    const HRESULT hr = SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
    ILFree(item);
    return SUCCEEDED(hr);
#else
    // Every desktop file manager on the bus implements FileManager1, and
    // ShowItems selects the file rather than merely opening its folder.
    {
        dbus::Connection& bus = dbus::Connection::Session();
        std::string ignored;
        if (bus.Open(ignored))
        {
            DBusMessage* message = dbus_message_new_method_call(
                "org.freedesktop.FileManager1", "/org/freedesktop/FileManager1",
                "org.freedesktop.FileManager1", "ShowItems");
            if (message != nullptr)
            {
                std::string uri = "file://";
                for (unsigned char c : path)
                {
                    if (std::isalnum(c) || c == '/' || c == '.' || c == '-' || c == '_' || c == '~')
                        uri += (char)c;
                    else
                    {
                        char escaped[4];
                        std::snprintf(escaped, sizeof(escaped), "%%%02X", c);
                        uri += escaped;
                    }
                }

                DBusMessageIter args;
                dbus_message_iter_init_append(message, &args);
                DBusMessageIter list;
                dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "s", &list);
                const char* uriText = uri.c_str();
                dbus_message_iter_append_basic(&list, DBUS_TYPE_STRING, &uriText);
                dbus_message_iter_close_container(&args, &list);
                const char* startupId = "";
                dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &startupId);

                DBusMessage* reply = bus.Call(message, 5000, ignored);
                dbus_message_unref(message);
                if (reply != nullptr)
                {
                    dbus_message_unref(reply);
                    return true;
                }
            }
        }
    }

    // No file manager on the bus: open the folder with whatever handles
    // folders, which at least gets the user to the right place.
    std::string folder = path;
    StripFilename(folder);
    const char* argv[] = { "xdg-open", folder.c_str(), nullptr };
    pid_t pid = 0;
    if (posix_spawnp(&pid, "xdg-open", nullptr, nullptr,
                     const_cast<char* const*>(argv), environ) != 0)
        return false;
    // xdg-open returns as soon as it has handed off; reaping it here keeps
    // a zombie from sitting in the process table until we exit.
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}
}
