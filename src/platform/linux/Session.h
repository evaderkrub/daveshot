#pragma once

#include "app/Geometry.h"

#include <cstdint>
#include <functional>
#include <string>

// What kind of desktop session this process is running in. Linux has two
// display servers with very different rules -- X11 lets a client read the
// screen and grab keys directly, Wayland lets it do neither and hands the
// job to the desktop portal instead -- so every Linux backend below picks its
// path from this one answer.
namespace daveshot::session
{
    // True on Wayland. Decided from SDL's video driver once the window is up,
    // and from the environment before that, so it gives the same answer
    // whether or not SDL has been initialised yet.
    bool IsWayland();

    // The application id the portals know us by. Set on the D-Bus connection
    // through the host registry and on the Wayland surface through SDL, so
    // the permission the user grants for screenshots and hotkeys is stored
    // against a name and does not have to be granted again on the next run.
    inline constexpr const char* kAppId = "org.daveshot.daveshot";

    // Our own window, named the way a portal's `parent_window` argument
    // wants it: "wayland:<xdg_foreign handle>" or "x11:<xid in hex>". The
    // desktop parents its permission dialogs to that window, and -- this is
    // the part that bites -- refuses to put a dialog on screen at all when
    // it cannot tell which window is asking. Empty while the window is
    // hidden, because a hidden window has no handle to export.
    //
    // Read through a provider rather than cached, so the answer is the
    // window's state at the moment of the call and not at the moment
    // somebody last remembered to publish it. Host installs the provider;
    // the portal backends read it.
    using ParentWindowProvider = std::function<std::string()>;
    void        SetParentWindowProvider(ParentWindowProvider provider);
    std::string ParentWindow();

    // The pixel rectangle a given SDL display occupies in capture space, so
    // the region overlay -- which on Wayland can only cover one display --
    // knows which part of the desktop capture it is showing.
    Rect CaptureBoundsOfDisplay(uint32_t sdlDisplayId);
}
