#include "platform/PrintScreenKey.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

namespace daveshot::printkey
{
namespace
{
    // The switch behind Settings > Accessibility > Keyboard > "Use the Print
    // screen key to open screen capture". The Snipping Tool's binding is the
    // shell's, not a RegisterHotKey we could win by asking first -- ours
    // registers happily and then both fire -- so this value is the only way
    // to stop it.
    const wchar_t* const kKeyPath = L"Control Panel\\Keyboard";
    const wchar_t* const kValue   = L"PrintScreenKeyForSnippingEnabled";

    // Windows 11 22H2 turned the Snipping Tool binding on by default and
    // ships without the value written, so "absent" means opposite things on
    // either side of that build: on Windows 10 it means the key is free.
    bool DefaultsToSnippingTool()
    {
        using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);

        const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll == nullptr)
            return false;

        // GetVersionEx reports Windows 8 to an unmanifested process; this is
        // the one call that answers with the build actually running.
        const auto rtlGetVersion =
            (RtlGetVersionFn)(void*)GetProcAddress(ntdll, "RtlGetVersion");
        if (rtlGetVersion == nullptr)
            return false;

        OSVERSIONINFOW info = {};
        info.dwOSVersionInfoSize = sizeof(info);
        if (rtlGetVersion(&info) != 0)
            return false;

        return info.dwBuildNumber >= 22621;
    }

    bool ReadEnabled(bool& enabled)
    {
        DWORD value = 0;
        DWORD size  = sizeof(value);
        const LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, kKeyPath, kValue,
                                            RRF_RT_DWORD, nullptr, &value, &size);
        if (status != ERROR_SUCCESS)
            return false;
        enabled = (value != 0);
        return true;
    }

    bool Write(DWORD value, std::string& error)
    {
        const LSTATUS status = RegSetKeyValueW(HKEY_CURRENT_USER, kKeyPath, kValue,
                                               REG_DWORD, &value, sizeof(value));
        if (status != ERROR_SUCCESS)
        {
            error = "Windows would not let us change the Print Screen setting (error " +
                    std::to_string((long)status) + ")";
            return false;
        }

        // The shell re-reads the keyboard settings when it is told they
        // changed; without the notice the change waits for the next sign-in.
        DWORD_PTR unused = 0;
        SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)kKeyPath,
                            SMTO_ABORTIFHUNG, 200, &unused);
        return true;
    }
}

Status Query()
{
    Status status;

    // A value that is there was written by the Settings app, by us, or by
    // another capture tool doing exactly this; either way it is the answer.
    bool enabled = false;
    if (!ReadEnabled(enabled))
        enabled = DefaultsToSnippingTool();

    if (enabled)
    {
        status.state = State::Desktop;
        status.hint  = "Windows opens the Snipping Tool when you press Print Screen.";
    }
    else
    {
        status.state = State::Ours;
        status.hint  = "Print Screen is daveshot's. Sign out and back in if the "
                       "Snipping Tool still opens.";
    }
    return status;
}

bool Take(std::string& error)
{
    return Write(0, error);
}

bool GiveBack(std::string& error)
{
    return Write(1, error);
}
}
