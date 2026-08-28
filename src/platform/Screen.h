#pragma once

#include "app/Geometry.h"
#include "app/Image.h"

#include <cstdint>
#include <string>
#include <vector>

namespace daveshot::screen
{
    struct MonitorInfo
    {
        std::string name;       // e.g. "Monitor 1 (2560x1440)"
        Rect        bounds;     // virtual-desktop coordinates, physical pixels
        bool        primary = false;
    };

    struct WindowInfo
    {
        // The OS handle, widened so nothing above this header includes
        // <windows.h>. Zero is never a valid window.
        uint64_t    handle = 0;
        std::string title;
        std::string process;    // executable name, to tell three "Untitled" apart
        Rect        bounds;     // the visible frame, not the resize border
    };

    // The union of every monitor. Its origin is the top-left of the primary
    // monitor, so x/y are negative for monitors above or left of it.
    Rect VirtualDesktopBounds();

    bool EnumerateMonitors(std::vector<MonitorInfo>& out, std::string& error);

    // Top-level, visible, non-cloaked windows with a title, most recently
    // active first, excluding our own. A window that vanishes between this
    // call and the capture simply fails the capture -- there is no way to
    // hold a window still, so the interface reports it and moves on.
    bool EnumerateWindows(std::vector<WindowInfo>& out, std::string& error);

    // Reads pixels straight off the screen. The rect is in virtual-desktop
    // coordinates and is clipped to it; an empty result is an error.
    bool CaptureRect(const Rect& area, Image& out, std::string& error);

    // Captures one window, including any part of it another window is
    // covering, by asking it to redraw into an off-screen surface. Falls back
    // to reading that region of the screen for windows that refuse.
    bool CaptureWindow(uint64_t handle, Image& out, std::string& error);

    // The window under a screen point, skipping our own -- for click-a-window
    // capture. Returns 0 when there is nothing usable there.
    uint64_t WindowAtPoint(int x, int y);
}
