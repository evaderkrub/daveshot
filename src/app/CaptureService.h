#pragma once

#include "app/Capture.h"
#include "app/Settings.h"

#include <string>

// The step between "the interface asked for a capture" and the OS calls that
// take one. It knows about the platform layer but nothing about ImGui, so the
// whole path from request to saved file can be exercised without a window.
namespace daveshot::capture
{
    // Where captures are written. An empty setting means Pictures\daveshot,
    // resolved here rather than stored, so a settings file that travels with
    // the folder does not carry one machine's user profile in it.
    std::string ResolveSaveFolder(const Settings& settings);

    // The whole virtual desktop, for the region overlay to freeze and draw.
    // bounds comes back too: the overlay needs the desktop origin to turn a
    // selection in image space back into screen coordinates.
    bool GrabDesktop(Image& out, Rect& bounds, std::string& error);

    // Window, Monitor and FullScreen requests. Region is not handled here --
    // it needs the overlay, so the interface grabs the desktop, lets the user
    // choose, and calls MakeShot on the crop.
    bool Grab(const CaptureRequest& request, Image& out, Rect& source, std::string& error);

    // Writes the shot to the configured folder and records the path on it.
    // Creates the folder if it has gone missing since the last capture.
    bool Save(Shot& shot, const Settings& settings, std::string& error);

    bool Copy(const Shot& shot, std::string& error);
}
