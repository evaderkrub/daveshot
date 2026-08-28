#pragma once

#include "app/Geometry.h"
#include "app/Image.h"
#include "app/Naming.h"

#include <cstdint>
#include <string>
#include <vector>

namespace daveshot
{
    enum class CaptureMode
    {
        Region,       // drag a rectangle on a frozen picture of the desktop
        Window,       // one top-level window, even where something covers it
        Monitor,      // one monitor, whole
        FullScreen,   // every monitor, as one image
    };

    const char* CaptureModeName(CaptureMode mode);

    struct CaptureRequest
    {
        CaptureMode mode         = CaptureMode::Region;
        int         monitorIndex = 0;    // Monitor mode
        uint64_t    windowHandle = 0;    // Window mode; 0 means "ask"
        int         delaySeconds = 0;
    };

    // One capture, with everything the interface needs to show it and
    // everything a later save needs to write it.
    struct Shot
    {
        unsigned    id = 0;          // stable for the life of the process
        Image       image;
        Image       thumbnail;       // small copy, so the history strip does
                                     // not hold a texture per full-size shot
        CaptureMode mode = CaptureMode::Region;
        Rect        source;          // where on the desktop it came from
        TimeParts   when;
        std::string label;           // "Region 1920x1080", for the strip
        std::string savedPath;       // empty until it is written to disk
    };

    // Fixed-capacity, newest first. Full-size pixels are the expensive part
    // -- a 4K shot is 33 MB -- so the limit is a memory decision as much as a
    // presentation one, and old shots are dropped rather than paged out.
    struct History
    {
        std::vector<Shot> shots;
        int               limit = 12;

        void Add(Shot shot);
        void Clear();
        const Shot* Find(unsigned id) const;
        Shot*       Find(unsigned id);

        size_t TotalBytes() const;
    };

    // Builds the label and thumbnail for a freshly captured image, and hands
    // out its id. Kept out of the capture backend so it can be tested without
    // a screen.
    Shot MakeShot(Image image, CaptureMode mode, const Rect& source, const TimeParts& when);

    // Longest edge of a history thumbnail, in pixels.
    inline constexpr int kThumbnailSize = 160;
}
