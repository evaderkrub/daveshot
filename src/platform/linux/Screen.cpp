#include "platform/Screen.h"

#include "platform/linux/ScreenBackends.h"
#include "platform/linux/Session.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

// The Linux screen layer. Monitors come from SDL on both display servers.
// Pixels and windows come from X11 where we are allowed to ask X directly,
// and from the desktop portal on Wayland, where the compositor answers on
// the user's behalf.

namespace daveshot::linuxscreen
{
namespace
{
    struct Layout
    {
        struct Entry
        {
            SDL_DisplayID id = 0;
            Rect          logical;
            float         scale = 1.0f;
            bool          primary = false;
        };
        std::vector<Entry> displays;
        Rect               logicalUnion;
        float              maxScale = 1.0f;
    };

    // Zero until a desktop capture has told us; before that the best guess
    // is the largest display scale, which is what GNOME renders its
    // screenshots at when monitors differ.
    float gKnownScale = 0.0f;

    Layout ReadLayout()
    {
        Layout layout;
        int count = 0;
        SDL_DisplayID* ids = SDL_GetDisplays(&count);
        const SDL_DisplayID primary = SDL_GetPrimaryDisplay();

        for (int i = 0; i < count; ++i)
        {
            SDL_Rect bounds{};
            if (!SDL_GetDisplayBounds(ids[i], &bounds))
                continue;

            Layout::Entry entry;
            entry.id      = ids[i];
            entry.logical = Rect{ bounds.x, bounds.y, bounds.w, bounds.h };
            entry.primary = (ids[i] == primary);
            entry.scale   = SDL_GetDisplayContentScale(ids[i]);
            if (!(entry.scale > 0.0f))
                entry.scale = 1.0f;

            if (layout.displays.empty())
                layout.logicalUnion = entry.logical;
            else
            {
                const int x0 = std::min(layout.logicalUnion.x, entry.logical.x);
                const int y0 = std::min(layout.logicalUnion.y, entry.logical.y);
                const int x1 = std::max(layout.logicalUnion.Right(),  entry.logical.Right());
                const int y1 = std::max(layout.logicalUnion.Bottom(), entry.logical.Bottom());
                layout.logicalUnion = Rect{ x0, y0, x1 - x0, y1 - y0 };
            }
            layout.maxScale = std::max(layout.maxScale, entry.scale);
            layout.displays.push_back(entry);
        }
        SDL_free(ids);
        return layout;
    }

    float ScaleFor(const Layout& layout)
    {
        // X11 coordinates are pixels already; only Wayland reports points.
        if (!session::IsWayland())
            return 1.0f;
        return (gKnownScale > 0.0f) ? gKnownScale : layout.maxScale;
    }

    Rect Scaled(const Rect& r, float scale)
    {
        if (scale == 1.0f)
            return r;
        return Rect{ (int)std::lround(r.x * scale), (int)std::lround(r.y * scale),
                     (int)std::lround(r.w * scale), (int)std::lround(r.h * scale) };
    }
}

Rect CaptureBoundsOfDisplay(uint32_t sdlDisplayId)
{
    const Layout layout = ReadLayout();
    const float  scale  = ScaleFor(layout);
    for (const Layout::Entry& entry : layout.displays)
        if (entry.id == sdlDisplayId)
            return Scaled(entry.logical, scale);
    return Rect{};
}

Rect DesktopBounds()
{
    const Layout layout = ReadLayout();
    return Scaled(layout.logicalUnion, ScaleFor(layout));
}

bool Monitors(std::vector<screen::MonitorInfo>& out, std::string& error)
{
    out.clear();
    const Layout layout = ReadLayout();
    if (layout.displays.empty())
    {
        error = "no monitors were reported";
        return false;
    }
    const float scale = ScaleFor(layout);

    for (const Layout::Entry& entry : layout.displays)
    {
        screen::MonitorInfo info;
        info.bounds  = Scaled(entry.logical, scale);
        info.primary = entry.primary;
        out.push_back(info);
    }

    // Primary first, then left to right: the order a user would list them in.
    std::stable_sort(out.begin(), out.end(),
                     [](const screen::MonitorInfo& a, const screen::MonitorInfo& b)
                     {
                         if (a.primary != b.primary) return a.primary;
                         return a.bounds.x < b.bounds.x;
                     });

    for (size_t i = 0; i < out.size(); ++i)
    {
        char label[128];
        std::snprintf(label, sizeof(label), "Monitor %d (%dx%d)%s",
                      (int)i + 1, out[i].bounds.w, out[i].bounds.h,
                      out[i].primary ? " - primary" : "");
        out[i].name = label;
    }
    return true;
}

void CalibrateCaptureScale(int imageWidth, int imageHeight)
{
    const Layout layout = ReadLayout();
    if (layout.logicalUnion.Empty())
        return;

    // Whichever of "points" or "points times the biggest scale" matches the
    // picture we were handed is the space the portal works in. Neither
    // matching -- monitors with different fractional scales -- keeps the
    // guess, and CaptureRect scales by the picture's own size anyway.
    const Rect atMax = Scaled(layout.logicalUnion, layout.maxScale);
    if (imageWidth == atMax.w && imageHeight == atMax.h)
        gKnownScale = layout.maxScale;
    else if (imageWidth == layout.logicalUnion.w && imageHeight == layout.logicalUnion.h)
        gKnownScale = 1.0f;
}
}

namespace daveshot::screen
{
namespace
{
    bool UsePortal()
    {
        return session::IsWayland();
    }

    // Lifts the requested rectangle out of a whole-desktop capture. The
    // capture is the desktop union with its own origin at (0,0); if its size
    // disagrees with what we computed, trust the pixels and scale the
    // request to fit rather than cropping the wrong place.
    bool CropFromDesktop(const Image& desktop, const Rect& area, Image& out, std::string& error)
    {
        const Rect bounds  = linuxscreen::DesktopBounds();
        const Rect clipped = Intersect(area, bounds);
        if (clipped.Empty() || bounds.Empty())
        {
            error = "that area is not on any monitor";
            return false;
        }

        const float sx = (float)desktop.width  / (float)bounds.w;
        const float sy = (float)desktop.height / (float)bounds.h;
        Rect local;
        local.x = (int)std::lround((clipped.x - bounds.x) * sx);
        local.y = (int)std::lround((clipped.y - bounds.y) * sy);
        local.w = (int)std::lround(clipped.w * sx);
        local.h = (int)std::lround(clipped.h * sy);

        if (local == Rect{ 0, 0, desktop.width, desktop.height })
        {
            out = desktop;
            return true;
        }
        if (!CropImage(desktop, local, out))
        {
            error = "that area is outside the captured desktop";
            return false;
        }
        return true;
    }
}

Features Capabilities()
{
    Features f;
    if (UsePortal())
    {
        // Wayland tells a client nothing about other clients' windows, and
        // a window cannot place itself across monitors, or at all.
        f.windowList          = false;
        f.pickOnScreen        = true;
        f.overlaySpansMonitors = false;
    }
    else
    {
        f.windowList          = true;
        f.pickOnScreen        = false;
        f.overlaySpansMonitors = true;
    }
    return f;
}

Rect VirtualDesktopBounds()
{
    return linuxscreen::DesktopBounds();
}

bool EnumerateMonitors(std::vector<MonitorInfo>& out, std::string& error)
{
    return linuxscreen::Monitors(out, error);
}

bool EnumerateWindows(std::vector<WindowInfo>& out, std::string& error)
{
    out.clear();
    if (UsePortal())
        return true;   // nothing to list, and that is not an error
    return x11screen::EnumerateWindows(out, error);
}

bool CaptureRect(const Rect& area, Image& out, std::string& error)
{
    if (!UsePortal())
        return x11screen::CaptureRect(area, out, error);

    Image desktop;
    if (!portalscreen::CaptureDesktop(desktop, error))
        return false;
    return CropFromDesktop(desktop, area, out, error);
}

bool CaptureWindow(uint64_t handle, Image& out, std::string& error)
{
    if (handle == kPickWindowOnScreen)
        return portalscreen::CaptureInteractive(out, error);
    if (UsePortal())
    {
        error = "windows cannot be listed on Wayland; use the on-screen picker";
        return false;
    }
    return x11screen::CaptureWindow(handle, out, error);
}

bool NeedsCapturePermission()
{
    return UsePortal() && portalscreen::NeedsPermission();
}

bool RequestCapturePermission(std::string& error)
{
    if (!NeedsCapturePermission())
        return true;
    return portalscreen::RequestPermission(error);
}

uint64_t WindowAtPoint(int x, int y)
{
    std::vector<WindowInfo> windows;
    std::string error;
    if (!EnumerateWindows(windows, error))
        return 0;
    // Front to back, so the first hit is the one on top.
    for (const WindowInfo& window : windows)
        if (window.bounds.Contains(x, y))
            return window.handle;
    return 0;
}
}
