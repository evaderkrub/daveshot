#include "platform/linux/Session.h"

#include "platform/linux/ScreenBackends.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <cstring>
#include <utility>

namespace daveshot::session
{
namespace
{
    // Main-thread only, like every portal call that reads it.
    ParentWindowProvider gParentWindow;
}

void SetParentWindowProvider(ParentWindowProvider provider)
{
    gParentWindow = std::move(provider);
}

std::string ParentWindow()
{
    return gParentWindow ? gParentWindow() : std::string();
}

bool IsWayland()
{
    // SDL knows for certain once it has picked a driver. Before that, the
    // session type is the next best evidence -- with WAYLAND_DISPLAY as a
    // tie-breaker for sessions that never set XDG_SESSION_TYPE.
    if (const char* driver = SDL_GetCurrentVideoDriver())
        return std::strcmp(driver, "wayland") == 0;

    if (const char* type = std::getenv("XDG_SESSION_TYPE"))
    {
        if (std::strcmp(type, "wayland") == 0) return true;
        if (std::strcmp(type, "x11") == 0)     return false;
    }
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    return wayland != nullptr && *wayland != '\0';
}

Rect CaptureBoundsOfDisplay(uint32_t sdlDisplayId)
{
    return linuxscreen::CaptureBoundsOfDisplay(sdlDisplayId);
}
}
