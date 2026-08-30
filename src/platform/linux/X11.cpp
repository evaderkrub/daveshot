#include "platform/linux/X11.h"

namespace daveshot::x11
{
namespace
{
    Display* gDisplay = nullptr;
    bool     gTried   = false;

    // Xlib error handlers are global and take no context, so the trap that
    // is currently active is recorded here. Traps do not nest across
    // threads; everything here runs on the one UI thread.
    ErrorTrap*    gActive = nullptr;
    unsigned char gLastCode = 0;

    int HandleError(Display*, XErrorEvent* event)
    {
        if (event != nullptr)
            gLastCode = event->error_code;
        return 0;
    }
}

Display* Get()
{
    if (!gTried)
    {
        gTried   = true;
        gDisplay = XOpenDisplay(nullptr);
    }
    return gDisplay;
}

ErrorTrap::ErrorTrap(Display* display)
    : m_display(display)
{
    gLastCode  = 0;
    gActive    = this;
    m_previous = XSetErrorHandler(HandleError);
}

ErrorTrap::~ErrorTrap()
{
    if (m_display != nullptr)
        XSync(m_display, False);
    XSetErrorHandler(m_previous);
    if (gActive == this)
        gActive = nullptr;
}

bool ErrorTrap::Failed()
{
    if (m_display != nullptr)
        XSync(m_display, False);
    m_code = gLastCode;
    return m_code != 0;
}
}
