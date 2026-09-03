#include "platform/SingleInstance.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace daveshot::instance
{
namespace
{
    // Local\ rather than Global\: one daveshot per signed-in session, not
    // one per machine -- two people on the same PC each get their own.
    const wchar_t* const kMutexName = L"Local\\org.daveshot.daveshot.instance";
    const wchar_t* const kEventName = L"Local\\org.daveshot.daveshot.show";

    HANDLE gMutex = nullptr;
    HANDLE gEvent = nullptr;
}

bool Claim()
{
    gMutex = CreateMutexW(nullptr, FALSE, kMutexName);
    if (gMutex == nullptr)
        return true;   // could not ask; start anyway

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        // Somebody else holds it. Their event is the doorbell; if they have
        // not created it yet they are still starting up and will show a
        // window of their own accord.
        if (HANDLE bell = OpenEventW(EVENT_MODIFY_STATE, FALSE, kEventName))
        {
            SetEvent(bell);
            CloseHandle(bell);
        }
        CloseHandle(gMutex);
        gMutex = nullptr;
        return false;
    }

    // Auto-reset: a wait that sees it signalled clears it, so each launch
    // rings once.
    gEvent = CreateEventW(nullptr, FALSE, FALSE, kEventName);
    return true;
}

void Release()
{
    if (gEvent) { CloseHandle(gEvent); gEvent = nullptr; }
    if (gMutex) { CloseHandle(gMutex); gMutex = nullptr; }
}

bool TakeWakeup()
{
    return gEvent != nullptr && WaitForSingleObject(gEvent, 0) == WAIT_OBJECT_0;
}
}
