#pragma once

#include "app/Geometry.h"
#include "app/Image.h"
#include "platform/Screen.h"

#include <cstdint>
#include <string>
#include <vector>

// The two ways of reading the screen on Linux, behind the one Screen.h
// interface. Screen.cpp picks between them; nothing else includes this.
namespace daveshot::linuxscreen
{
    // Monitor layout is SDL's job on both display servers -- it is the one
    // source that agrees with where our own window is. On X11 SDL reports
    // pixels; on Wayland it reports logical points, and the capture arrives
    // in pixels, so these convert to "capture space" using the scale the
    // desktop screenshot turned out to have.
    Rect CaptureBoundsOfDisplay(uint32_t sdlDisplayId);
    Rect DesktopBounds();
    bool Monitors(std::vector<screen::MonitorInfo>& out, std::string& error);

    // Records the size the desktop capture actually came back at, so the
    // logical-to-pixel scale stops being a guess. Called by the portal
    // backend after every whole-desktop shot.
    void CalibrateCaptureScale(int imageWidth, int imageHeight);
}

namespace daveshot::x11screen
{
    bool Available();
    bool CaptureRect(const Rect& area, Image& out, std::string& error);
    bool EnumerateWindows(std::vector<screen::WindowInfo>& out, std::string& error);
    bool CaptureWindow(uint64_t handle, Image& out, std::string& error);
}

namespace daveshot::portalscreen
{
    // The whole desktop as one image, in the portal's pixel space.
    bool CaptureDesktop(Image& out, std::string& error);

    // Hands control to the desktop's own picker (region, window or screen,
    // as the desktop offers) and returns whatever the user chose.
    bool CaptureInteractive(Image& out, std::string& error);

    // Whether the user has yet to say daveshot may take screenshots, and the
    // request that asks. See the note at the top of ScreenPortal.cpp for why
    // this cannot simply be left to the first capture.
    bool NeedsPermission();
    bool RequestPermission(std::string& error);
}
