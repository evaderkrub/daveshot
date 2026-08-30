#pragma once

#include <X11/Xlib.h>

#include <string>

// The process's own X connection, separate from SDL's. SDL owns its display
// and its event queue; grabbing keys or reading the root window through it
// would mean fishing our events out from under it. A second connection is
// cheap and keeps the two apart.
namespace daveshot::x11
{
    // Opened on first use; nullptr when there is no X server to talk to.
    Display* Get();

    // Xlib reports failures through a process-wide handler, and the default
    // one exits the process. This swaps in a handler that just remembers the
    // error for the scope, so a refused grab or a vanished window comes back
    // as a false return rather than an abort.
    class ErrorTrap
    {
    public:
        explicit ErrorTrap(Display* display);
        ~ErrorTrap();

        ErrorTrap(const ErrorTrap&)            = delete;
        ErrorTrap& operator=(const ErrorTrap&) = delete;

        // Flushes the queue so any error the server sent has arrived, then
        // says whether one did.
        bool Failed();
        unsigned char Code() const { return m_code; }

    private:
        Display*     m_display = nullptr;
        XErrorHandler m_previous = nullptr;
        unsigned char m_code = 0;
    };
}
