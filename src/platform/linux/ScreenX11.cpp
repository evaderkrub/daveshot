#include "platform/linux/ScreenBackends.h"

#include "platform/linux/X11.h"

#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <cstdio>
#include <cstring>
#include <unistd.h>

// Reading the screen on X11. X lets any client read the root window and
// walk every other client's top-level windows, which is all a screenshot
// tool needs -- and all it can do: without the Composite extension there is
// no way to see a window through the one covering it, so window capture here
// is "that rectangle of the screen".

namespace daveshot::x11screen
{
namespace
{
    // A property fetched whole. X returns properties in chunks of 32-bit
    // words in an encoding that depends on the format; this hides that.
    struct Property
    {
        std::vector<unsigned char> data;
        Atom                       type   = None;
        int                        format = 0;
        unsigned long              count  = 0;

        bool Read(Display* display, Window window, Atom name, Atom wanted)
        {
            data.clear();
            count = 0;

            Atom          actualType = None;
            int           actualFormat = 0;
            unsigned long items = 0, remaining = 0;
            unsigned char* raw = nullptr;

            x11::ErrorTrap trap(display);
            const int status = XGetWindowProperty(display, window, name, 0, 1 << 20, False,
                                                  wanted, &actualType, &actualFormat,
                                                  &items, &remaining, &raw);
            if (trap.Failed() || status != Success || raw == nullptr)
            {
                if (raw) XFree(raw);
                return false;
            }
            type   = actualType;
            format = actualFormat;
            count  = items;
            // Xlib hands 32-bit properties back as native longs, whatever
            // their width on this machine.
            const size_t unit = (actualFormat == 32) ? sizeof(long) : (size_t)actualFormat / 8;
            data.assign(raw, raw + items * unit);
            XFree(raw);
            return items > 0;
        }

        long At(size_t index) const
        {
            if (format == 32)
            {
                long value = 0;
                std::memcpy(&value, data.data() + index * sizeof(long), sizeof(long));
                return value;
            }
            if (format == 16)
            {
                unsigned short value = 0;
                std::memcpy(&value, data.data() + index * 2, 2);
                return value;
            }
            return data[index];
        }
    };

    Atom Intern(Display* display, const char* name)
    {
        return XInternAtom(display, name, False);
    }

    std::string ReadTitle(Display* display, Window window)
    {
        Property prop;
        if (prop.Read(display, window, Intern(display, "_NET_WM_NAME"),
                      Intern(display, "UTF8_STRING")) && prop.format == 8)
            return std::string(reinterpret_cast<const char*>(prop.data.data()), prop.count);

        // Older clients only set the ICCCM name, which is Latin-1 in theory
        // and ASCII in practice.
        char* name = nullptr;
        x11::ErrorTrap trap(display);
        if (XFetchName(display, window, &name) != 0 && name != nullptr)
        {
            std::string title(name);
            XFree(name);
            if (!trap.Failed())
                return title;
        }
        return std::string();
    }

    long ReadCardinal(Display* display, Window window, const char* name, long fallback)
    {
        Property prop;
        if (prop.Read(display, window, Intern(display, name), XA_CARDINAL) && prop.format == 32)
            return prop.At(0);
        return fallback;
    }

    bool HasAtom(Display* display, Window window, const char* property, const char* value)
    {
        Property prop;
        if (!prop.Read(display, window, Intern(display, property), XA_ATOM))
            return false;
        const Atom wanted = Intern(display, value);
        for (unsigned long i = 0; i < prop.count; ++i)
            if ((Atom)prop.At(i) == wanted)
                return true;
        return false;
    }

    std::string ProcessNameOf(long pid)
    {
        if (pid <= 0)
            return std::string();
        char path[64];
        std::snprintf(path, sizeof(path), "/proc/%ld/comm", pid);
        std::FILE* f = std::fopen(path, "r");
        if (f == nullptr)
            return std::string();
        char name[256] = {};
        const size_t got = std::fread(name, 1, sizeof(name) - 1, f);
        std::fclose(f);
        std::string result(name, got);
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
            result.pop_back();
        return result;
    }

    // The rectangle the user would point at. The client window's own
    // geometry misses the title bar the window manager drew around it, and
    // a client drawing its own decorations pads its window with an invisible
    // shadow margin; the two extents properties describe exactly those.
    bool FrameBounds(Display* display, Window window, Rect& out)
    {
        XWindowAttributes attributes{};
        x11::ErrorTrap trap(display);
        if (XGetWindowAttributes(display, window, &attributes) == 0 || trap.Failed())
            return false;
        if (attributes.map_state != IsViewable)
            return false;

        int    rootX = 0, rootY = 0;
        Window child = None;
        XTranslateCoordinates(display, window, attributes.root, 0, 0, &rootX, &rootY, &child);
        if (trap.Failed())
            return false;

        Rect r{ rootX, rootY, attributes.width, attributes.height };

        Property extents;
        if (extents.Read(display, window, Intern(display, "_NET_FRAME_EXTENTS"), XA_CARDINAL) &&
            extents.count >= 4)
        {
            const long left = extents.At(0), right = extents.At(1);
            const long top  = extents.At(2), bottom = extents.At(3);
            r.x -= (int)left;
            r.y -= (int)top;
            r.w += (int)(left + right);
            r.h += (int)(top + bottom);
        }
        if (extents.Read(display, window, Intern(display, "_GTK_FRAME_EXTENTS"), XA_CARDINAL) &&
            extents.count >= 4)
        {
            const long left = extents.At(0), right = extents.At(1);
            const long top  = extents.At(2), bottom = extents.At(3);
            r.x += (int)left;
            r.y += (int)top;
            r.w -= (int)(left + right);
            r.h -= (int)(top + bottom);
        }

        out = r;
        return !r.Empty();
    }

    bool IsListable(Display* display, Window window, long currentDesktop)
    {
        if (HasAtom(display, window, "_NET_WM_STATE", "_NET_WM_STATE_HIDDEN"))
            return false;   // minimised

        // Panels, the desktop, tooltips and menus are on screen but are not
        // what anyone means by "a window".
        for (const char* type : { "_NET_WM_WINDOW_TYPE_DOCK", "_NET_WM_WINDOW_TYPE_DESKTOP",
                                  "_NET_WM_WINDOW_TYPE_TOOLTIP", "_NET_WM_WINDOW_TYPE_NOTIFICATION",
                                  "_NET_WM_WINDOW_TYPE_SPLASH", "_NET_WM_WINDOW_TYPE_MENU",
                                  "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", "_NET_WM_WINDOW_TYPE_POPUP_MENU" })
        {
            if (HasAtom(display, window, "_NET_WM_WINDOW_TYPE", type))
                return false;
        }

        const long desktop = ReadCardinal(display, window, "_NET_WM_DESKTOP", -1);
        if (desktop >= 0 && currentDesktop >= 0 && desktop != currentDesktop &&
            desktop != 0xFFFFFFFFL)
            return false;   // on another workspace

        return true;
    }

    // Straight copy from an XImage into RGBA. The common case -- 32 bits per
    // pixel, the usual masks -- is a tight loop; anything else goes through
    // XGetPixel, which is correct for every visual and slow for all of them.
    void ConvertImage(XImage* image, Image& out)
    {
        const int width  = image->width;
        const int height = image->height;
        out.width  = width;
        out.height = height;
        out.pixels.resize((size_t)width * (size_t)height * 4u);

        auto shiftOf = [](unsigned long mask)
        {
            int shift = 0;
            while (mask != 0 && (mask & 1u) == 0) { mask >>= 1; ++shift; }
            return shift;
        };
        const int rShift = shiftOf(image->red_mask);
        const int gShift = shiftOf(image->green_mask);
        const int bShift = shiftOf(image->blue_mask);

        const bool fast = image->bits_per_pixel == 32 && image->byte_order == LSBFirst;
        for (int y = 0; y < height; ++y)
        {
            uint8_t* dst = out.pixels.data() + (size_t)y * (size_t)width * 4u;
            const uint8_t* row = reinterpret_cast<const uint8_t*>(image->data)
                               + (size_t)y * (size_t)image->bytes_per_line;
            for (int x = 0; x < width; ++x)
            {
                unsigned long pixel;
                if (fast)
                {
                    uint32_t v;
                    std::memcpy(&v, row + (size_t)x * 4u, 4);
                    pixel = v;
                }
                else
                    pixel = XGetPixel(image, x, y);

                dst[x * 4 + 0] = (uint8_t)((pixel & image->red_mask)   >> rShift);
                dst[x * 4 + 1] = (uint8_t)((pixel & image->green_mask) >> gShift);
                dst[x * 4 + 2] = (uint8_t)((pixel & image->blue_mask)  >> bShift);
                dst[x * 4 + 3] = 255;   // the screen has no alpha
            }
        }
    }
}

bool Available()
{
    return x11::Get() != nullptr;
}

bool CaptureRect(const Rect& area, Image& out, std::string& error)
{
    Display* display = x11::Get();
    if (display == nullptr)
    {
        error = "no X display is available";
        return false;
    }

    const Window root = DefaultRootWindow(display);
    XWindowAttributes rootAttributes{};
    XGetWindowAttributes(display, root, &rootAttributes);
    const Rect screen{ 0, 0, rootAttributes.width, rootAttributes.height };

    const Rect clipped = Intersect(area, screen);
    if (clipped.Empty())
    {
        error = "that area is not on any monitor";
        return false;
    }

    x11::ErrorTrap trap(display);
    XImage* image = XGetImage(display, root, clipped.x, clipped.y,
                              (unsigned)clipped.w, (unsigned)clipped.h, AllPlanes, ZPixmap);
    if (image == nullptr || trap.Failed())
    {
        if (image) XDestroyImage(image);
        error = "the X server refused to read the screen";
        return false;
    }

    ConvertImage(image, out);
    XDestroyImage(image);
    return true;
}

bool EnumerateWindows(std::vector<screen::WindowInfo>& out, std::string& error)
{
    out.clear();
    Display* display = x11::Get();
    if (display == nullptr)
    {
        error = "no X display is available";
        return false;
    }

    const Window root = DefaultRootWindow(display);

    Property stacking;
    if (!stacking.Read(display, root, Intern(display, "_NET_CLIENT_LIST_STACKING"), XA_WINDOW))
        return true;   // a window manager without EWMH: nothing to offer

    const long currentDesktop = ReadCardinal(display, root, "_NET_CURRENT_DESKTOP", -1);
    const long ourPid = (long)getpid();

    // The list is bottom to top; a picker wants the front-most first.
    for (unsigned long i = stacking.count; i > 0; --i)
    {
        const Window window = (Window)stacking.At(i - 1);

        const long pid = ReadCardinal(display, window, "_NET_WM_PID", 0);
        if (pid == ourPid)
            continue;   // never offer to capture ourselves
        if (!IsListable(display, window, currentDesktop))
            continue;

        screen::WindowInfo info;
        if (!FrameBounds(display, window, info.bounds))
            continue;
        info.title = ReadTitle(display, window);
        if (info.title.empty())
            continue;
        info.handle  = (uint64_t)window;
        info.process = ProcessNameOf(pid);
        out.push_back(info);
    }
    return true;
}

bool CaptureWindow(uint64_t handle, Image& out, std::string& error)
{
    Display* display = x11::Get();
    if (display == nullptr)
    {
        error = "no X display is available";
        return false;
    }

    Rect bounds;
    if (!FrameBounds(display, (Window)handle, bounds))
    {
        error = "that window has closed";
        return false;
    }
    return CaptureRect(bounds, out, error);
}
}
