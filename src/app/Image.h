#pragma once

#include "app/Geometry.h"

#include <cstdint>
#include <vector>

namespace daveshot
{
    // A captured image: 8-bit RGBA, top-down, tightly packed.
    //
    // One layout, chosen once, for the whole application. The platform layer
    // converts into it (Windows hands back bottom-up BGRA), the interface
    // uploads it to a texture as-is, and the writers convert back out. Every
    // conversion that exists is at an edge, not in the middle.
    struct Image
    {
        int                  width  = 0;
        int                  height = 0;
        std::vector<uint8_t> pixels;   // width * height * 4

        bool Valid() const
        {
            return width > 0 && height > 0 &&
                   pixels.size() == (size_t)width * (size_t)height * 4u;
        }

        size_t ByteSize() const { return pixels.size(); }

        void Reset()
        {
            width = 0;
            height = 0;
            pixels.clear();
            pixels.shrink_to_fit();
        }
    };

    // Shrinks an image to fit inside maxWidth x maxHeight, preserving aspect
    // ratio. Box-averages the source pixels rather than point-sampling: a
    // thumbnail of a screenshot is mostly text, and nearest-neighbour turns
    // text into noise. An image already small enough is copied unchanged.
    bool ScaleImageToFit(const Image& source, int maxWidth, int maxHeight, Image& out);

    // Copies a sub-rectangle out of an image. The rect is in image space --
    // callers holding screen coordinates subtract the capture origin first.
    // Returns false if the rect does not overlap the image at all.
    bool CropImage(const Image& source, const Rect& area, Image& out);
}
