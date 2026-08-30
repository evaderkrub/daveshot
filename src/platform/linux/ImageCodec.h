#pragma once

#include "app/Image.h"

#include <cstdint>
#include <string>
#include <vector>

// PNG and JPEG in memory, through stb. Linux has no system image codec the
// way Windows has WIC, and linking libpng would put a shared library between
// the build and "copy the folder and run it" -- stb compiles straight in.
//
// Shared by the writer (a file is the encoded bytes on disk), the clipboard
// (X11 and Wayland exchange images as PNG) and the portal path (which hands
// captures back as PNG files).
namespace daveshot::codec
{
    bool EncodePng(const Image& image, std::vector<uint8_t>& out, std::string& error);
    bool EncodeJpeg(const Image& image, int quality, std::vector<uint8_t>& out,
                    std::string& error);

    // Decodes to the application's one layout: RGBA, top-down, tightly packed.
    bool DecodeFile(const std::string& path, Image& out, std::string& error);
}
