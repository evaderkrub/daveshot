#include "platform/Autostart.h"

#include "platform/Paths.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cwctype>
#include <string>

namespace daveshot::autostart
{
namespace
{
    // The per-user Run key: no elevation, and Task Manager's Startup tab
    // lists what is in it, so the user can see and disable it from there
    // as well as from here.
    const wchar_t* const kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t* const kValue  = L"daveshot";

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

    // Quoted, because a path under Program Files or a user's name has a
    // space in it, and --background so a sign-in does not open a window
    // over whatever the person sat down to do.
    std::wstring Command()
    {
        return L"\"" + Utf8ToWide(paths::ExePath()) + L"\" --background";
    }

    bool SameCommand(const std::wstring& a, const std::wstring& b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
        {
            const wchar_t x = a[i] == L'/' ? L'\\' : a[i];
            const wchar_t y = b[i] == L'/' ? L'\\' : b[i];
            if (std::towlower(x) != std::towlower(y))
                return false;
        }
        return true;
    }

    bool ReadCommand(std::wstring& out)
    {
        DWORD bytes = 0;
        if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kValue, RRF_RT_REG_SZ,
                         nullptr, nullptr, &bytes) != ERROR_SUCCESS || bytes < sizeof(wchar_t))
            return false;

        out.resize(bytes / sizeof(wchar_t));
        if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kValue, RRF_RT_REG_SZ,
                         nullptr, out.data(), &bytes) != ERROR_SUCCESS)
            return false;

        // The size includes the terminator, and the string must not.
        while (!out.empty() && out.back() == L'\0')
            out.pop_back();
        return true;
    }
}

State Query()
{
    // A value that names another copy of daveshot -- a folder that has
    // since moved, or a second install -- is not this one starting at
    // login, and the honest answer is "off". Ticking the box then points
    // the entry here.
    std::wstring command;
    if (!ReadCommand(command))
        return State::Off;
    return SameCommand(command, Command()) ? State::On : State::Off;
}

bool Enable(std::string& error)
{
    const std::wstring command = Command();
    const DWORD bytes = (DWORD)((command.size() + 1) * sizeof(wchar_t));
    const LSTATUS status = RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kValue,
                                           REG_SZ, command.c_str(), bytes);
    if (status != ERROR_SUCCESS)
    {
        error = "Windows would not let us register daveshot to start at sign-in (error " +
                std::to_string((long)status) + ")";
        return false;
    }
    return true;
}

bool Disable(std::string& error)
{
    const LSTATUS status = RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kValue);
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND)
    {
        error = "Windows would not let us remove daveshot from the sign-in list (error " +
                std::to_string((long)status) + ")";
        return false;
    }
    return true;
}

const char* Label()
{
    return "Start with Windows";
}
}
