#pragma once

#include "app/Image.h"

#include <string>

namespace daveshot::imageio
{
    enum class Format
    {
        Png,    // lossless, keeps text crisp -- the default for screenshots
        Jpeg,   // for when a shot of a photo or a video frame is being mailed
    };

    const char* Extension(Format format);
    const char* FormatName(Format format);

    // Writes an image to disk. Uses the Windows Imaging Component, which
    // every Windows machine already has -- so no encoder gets linked in and
    // no DLL travels with the build.
    //
    // quality is 1..100 and only applies to JPEG.
    bool Save(const std::string& path, const Image& image,
              Format format, int quality, std::string& error);
}
