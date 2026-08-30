#include "platform/Screen.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <psapi.h>

#include <algorithm>
#include <cstdio>

namespace daveshot::screen
{
namespace
{
    Rect FromRECT(const RECT& r)
    {
        Rect out;
        out.x = r.left;
        out.y = r.top;
        out.w = r.right - r.left;
        out.h = r.bottom - r.top;
        return out;
    }

    std::string WideToUtf8(const std::wstring& wide)
    {
        if (wide.empty())
            return std::string();
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(),
                                              nullptr, 0, nullptr, nullptr);
        if (bytes <= 0)
            return std::string();
        std::string out((size_t)bytes, 0);
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(),
                            out.data(), bytes, nullptr, nullptr);
        return out;
    }

    std::string LastErrorText(const char* what)
    {
        const DWORD code = GetLastError();
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer), "%s failed (error %lu)", what, code);
        return std::string(buffer);
    }

    // Copies a device-independent bitmap out of a DC into our RGBA layout.
    //
    // GetDIBits hands back BGRA bottom-up by default. Asking for a negative
    // height gets it top-down, which saves a second pass, and the channel
    // swap is folded into the same loop.
    bool ReadBitmapPixels(HDC dc, HBITMAP bitmap, int width, int height,
                          Image& out, std::string& error)
    {
        BITMAPINFO info{};
        info.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth       = width;
        info.bmiHeader.biHeight      = -height;      // top-down
        info.bmiHeader.biPlanes      = 1;
        info.bmiHeader.biBitCount    = 32;
        info.bmiHeader.biCompression = BI_RGB;

        std::vector<uint8_t> raw((size_t)width * (size_t)height * 4u);
        if (GetDIBits(dc, bitmap, 0, (UINT)height, raw.data(), &info, DIB_RGB_COLORS) == 0)
        {
            error = LastErrorText("GetDIBits");
            return false;
        }

        out.width  = width;
        out.height = height;
        out.pixels.resize(raw.size());

        // The screen has no meaningful alpha channel -- GDI leaves it zero
        // for most sources, which would make the whole capture transparent in
        // any consumer that respects it. Force it opaque.
        const size_t count = (size_t)width * (size_t)height;
        for (size_t i = 0; i < count; ++i)
        {
            out.pixels[i * 4 + 0] = raw[i * 4 + 2];   // R <- B
            out.pixels[i * 4 + 1] = raw[i * 4 + 1];   // G
            out.pixels[i * 4 + 2] = raw[i * 4 + 0];   // B <- R
            out.pixels[i * 4 + 3] = 255;
        }
        return true;
    }

    struct MonitorCollector
    {
        std::vector<MonitorInfo>* out;
        int                       index;
    };

    BOOL CALLBACK MonitorProc(HMONITOR monitor, HDC, LPRECT, LPARAM param)
    {
        MonitorCollector* collector = reinterpret_cast<MonitorCollector*>(param);

        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, &info) == 0)
            return TRUE;   // skip it; one unreadable monitor is not fatal

        MonitorInfo entry;
        entry.bounds  = FromRECT(info.rcMonitor);
        entry.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;

        // Named after the sort below, not here: numbering during enumeration
        // and sorting afterwards left the primary monitor labelled "2".
        collector->out->push_back(entry);
        ++collector->index;
        return TRUE;
    }

    bool IsCloaked(HWND window)
    {
        // A window can be visible by the old rules and still not on screen:
        // this is how the shell parks store apps and how virtual desktops
        // hide windows belonging to another desktop. Without this check the
        // window list fills up with things the user cannot see.
        DWORD cloaked = 0;
        if (DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)) != S_OK)
            return false;
        return cloaked != 0;
    }

    // DWM draws a window's shadow and resize border outside its visible
    // frame, so GetWindowRect is several pixels bigger than what the user
    // sees. The extended frame bounds are the rectangle they would point at.
    Rect VisibleFrameBounds(HWND window)
    {
        RECT frame{};
        if (DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS,
                                  &frame, sizeof(frame)) == S_OK)
            return FromRECT(frame);

        RECT fallback{};
        GetWindowRect(window, &fallback);
        return FromRECT(fallback);
    }

    std::string ProcessNameOf(HWND window)
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (pid == 0)
            return std::string();

        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (process == nullptr)
            return std::string();

        wchar_t path[MAX_PATH] = {};
        DWORD   size = MAX_PATH;
        std::wstring name;
        if (QueryFullProcessImageNameW(process, 0, path, &size) != 0)
        {
            name = path;
            const size_t cut = name.find_last_of(L"\\/");
            if (cut != std::wstring::npos)
                name = name.substr(cut + 1);
        }
        CloseHandle(process);
        return WideToUtf8(name);
    }

    BOOL CALLBACK WindowProc(HWND window, LPARAM param)
    {
        auto* out = reinterpret_cast<std::vector<WindowInfo>*>(param);

        if (IsWindowVisible(window) == 0 || IsIconic(window) != 0)
            return TRUE;
        if (GetWindow(window, GW_OWNER) != nullptr)
            return TRUE;   // dialogs and palettes follow their owner

        const LONG_PTR exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
        if ((exStyle & WS_EX_TOOLWINDOW) != 0)
            return TRUE;

        if (IsCloaked(window))
            return TRUE;

        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (pid == GetCurrentProcessId())
            return TRUE;   // never offer to capture ourselves

        const int length = GetWindowTextLengthW(window);
        if (length <= 0)
            return TRUE;

        std::wstring title((size_t)length, 0);
        GetWindowTextW(window, title.data(), length + 1);

        WindowInfo info;
        info.handle  = (uint64_t)(uintptr_t)window;
        info.title   = WideToUtf8(title);
        info.process = ProcessNameOf(window);
        info.bounds  = VisibleFrameBounds(window);

        if (info.bounds.Empty())
            return TRUE;

        out->push_back(info);
        return TRUE;
    }
}

Features Capabilities()
{
    return Features{};   // the defaults describe Windows
}

Rect VirtualDesktopBounds()
{
    Rect r;
    r.x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    r.y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    r.w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    r.h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return r;
}

bool EnumerateMonitors(std::vector<MonitorInfo>& out, std::string& error)
{
    out.clear();
    MonitorCollector collector{ &out, 0 };
    if (EnumDisplayMonitors(nullptr, nullptr, MonitorProc,
                            reinterpret_cast<LPARAM>(&collector)) == 0)
    {
        error = LastErrorText("EnumDisplayMonitors");
        return false;
    }
    if (out.empty())
    {
        error = "no monitors were reported";
        return false;
    }

    // Primary first, then left to right: the order a user would list them in.
    std::stable_sort(out.begin(), out.end(),
                     [](const MonitorInfo& a, const MonitorInfo& b)
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

bool EnumerateWindows(std::vector<WindowInfo>& out, std::string& error)
{
    out.clear();
    // EnumWindows walks in z-order, front to back, which is already the
    // "most recently used first" order a picker wants.
    if (EnumWindows(WindowProc, reinterpret_cast<LPARAM>(&out)) == 0 && GetLastError() != 0)
    {
        error = LastErrorText("EnumWindows");
        return false;
    }
    return true;
}

bool CaptureRect(const Rect& area, Image& out, std::string& error)
{
    const Rect clipped = Intersect(area, VirtualDesktopBounds());
    if (clipped.Empty())
    {
        error = "that area is not on any monitor";
        return false;
    }

    HDC screenDc = GetDC(nullptr);
    if (screenDc == nullptr)
    {
        error = LastErrorText("GetDC");
        return false;
    }

    bool    ok      = false;
    HDC     memoryDc = CreateCompatibleDC(screenDc);
    HBITMAP bitmap   = nullptr;
    HGDIOBJ previous = nullptr;

    if (memoryDc == nullptr)
    {
        error = LastErrorText("CreateCompatibleDC");
    }
    else if ((bitmap = CreateCompatibleBitmap(screenDc, clipped.w, clipped.h)) == nullptr)
    {
        error = LastErrorText("CreateCompatibleBitmap");
    }
    else
    {
        previous = SelectObject(memoryDc, bitmap);
        // CAPTUREBLT includes layered windows -- tooltips, menus with
        // transparency, and the drop shadows around them. Without it those
        // come out as holes.
        if (BitBlt(memoryDc, 0, 0, clipped.w, clipped.h,
                   screenDc, clipped.x, clipped.y, SRCCOPY | CAPTUREBLT) == 0)
        {
            error = LastErrorText("BitBlt");
        }
        else
        {
            SelectObject(memoryDc, previous);
            previous = nullptr;
            ok = ReadBitmapPixels(memoryDc, bitmap, clipped.w, clipped.h, out, error);
        }
    }

    if (previous != nullptr) SelectObject(memoryDc, previous);
    if (bitmap   != nullptr) DeleteObject(bitmap);
    if (memoryDc != nullptr) DeleteDC(memoryDc);
    ReleaseDC(nullptr, screenDc);
    return ok;
}

bool CaptureWindow(uint64_t handle, Image& out, std::string& error)
{
    HWND window = (HWND)(uintptr_t)handle;
    if (window == nullptr || IsWindow(window) == 0)
    {
        error = "that window has closed";
        return false;
    }

    const Rect bounds = VisibleFrameBounds(window);
    if (bounds.Empty())
    {
        error = "that window has no visible area";
        return false;
    }

    // PrintWindow asks the window to draw itself, so anything covering it
    // does not end up in the shot. PW_RENDERFULLCONTENT is what makes it work
    // for the DirectComposition-rendered windows (browsers, most Electron
    // apps) that used to come back blank.
    HDC     screenDc = GetDC(nullptr);
    HDC     memoryDc = CreateCompatibleDC(screenDc);
    HBITMAP bitmap   = CreateCompatibleBitmap(screenDc, bounds.w, bounds.h);

    bool ok = false;
    if (memoryDc != nullptr && bitmap != nullptr)
    {
        HGDIOBJ previous = SelectObject(memoryDc, bitmap);
        const BOOL printed = PrintWindow(window, memoryDc, PW_RENDERFULLCONTENT);
        SelectObject(memoryDc, previous);

        if (printed != 0)
            ok = ReadBitmapPixels(memoryDc, bitmap, bounds.w, bounds.h, out, error);
    }

    if (bitmap   != nullptr) DeleteObject(bitmap);
    if (memoryDc != nullptr) DeleteDC(memoryDc);
    ReleaseDC(nullptr, screenDc);

    // Some windows refuse to render on demand. Reading that region of the
    // screen still gets the user a picture -- with whatever is on top of it,
    // which is worse than nothing only if we say nothing about it.
    if (!ok)
        return CaptureRect(bounds, out, error);

    return true;
}

bool NeedsCapturePermission()
{
    return false;   // Windows lets any process read the screen
}

bool RequestCapturePermission(std::string&)
{
    return true;
}

uint64_t WindowAtPoint(int x, int y)
{
    POINT point{ x, y };
    HWND  window = WindowFromPoint(point);
    if (window == nullptr)
        return 0;

    // WindowFromPoint lands on a child control; walk up to the top-level
    // window, which is what the user means when they click "that window".
    HWND root = GetAncestor(window, GA_ROOT);
    if (root == nullptr)
        return 0;

    DWORD pid = 0;
    GetWindowThreadProcessId(root, &pid);
    if (pid == GetCurrentProcessId())
        return 0;

    return (uint64_t)(uintptr_t)root;
}
}
