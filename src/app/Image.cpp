#include "app/Image.h"

#include <cstring>

namespace daveshot
{
Rect RectFromCorners(int x0, int y0, int x1, int y1)
{
    Rect r;
    r.x = (x0 < x1) ? x0 : x1;
    r.y = (y0 < y1) ? y0 : y1;
    r.w = (x0 < x1) ? (x1 - x0) : (x0 - x1);
    r.h = (y0 < y1) ? (y1 - y0) : (y0 - y1);
    return r;
}

Rect Intersect(const Rect& a, const Rect& b)
{
    const int left   = (a.x > b.x) ? a.x : b.x;
    const int top    = (a.y > b.y) ? a.y : b.y;
    const int right  = (a.Right()  < b.Right())  ? a.Right()  : b.Right();
    const int bottom = (a.Bottom() < b.Bottom()) ? a.Bottom() : b.Bottom();

    Rect r;
    if (right <= left || bottom <= top)
        return r;   // empty

    r.x = left;
    r.y = top;
    r.w = right - left;
    r.h = bottom - top;
    return r;
}

bool CropImage(const Image& source, const Rect& area, Image& out)
{
    if (!source.Valid())
        return false;

    const Rect bounds{ 0, 0, source.width, source.height };
    const Rect clipped = Intersect(area, bounds);
    if (clipped.Empty())
        return false;

    out.width  = clipped.w;
    out.height = clipped.h;
    out.pixels.assign((size_t)clipped.w * (size_t)clipped.h * 4u, 0);

    const size_t sourceStride = (size_t)source.width * 4u;
    const size_t destStride   = (size_t)clipped.w * 4u;
    for (int row = 0; row < clipped.h; ++row)
    {
        const uint8_t* src = source.pixels.data()
                           + (size_t)(clipped.y + row) * sourceStride
                           + (size_t)clipped.x * 4u;
        std::memcpy(out.pixels.data() + (size_t)row * destStride, src, destStride);
    }
    return true;
}
}

namespace daveshot
{
bool ScaleImageToFit(const Image& source, int maxWidth, int maxHeight, Image& out)
{
    if (!source.Valid() || maxWidth <= 0 || maxHeight <= 0)
        return false;

    if (source.width <= maxWidth && source.height <= maxHeight)
    {
        out = source;
        return true;
    }

    const double scale = (double)maxWidth / (double)source.width < (double)maxHeight / (double)source.height
                       ? (double)maxWidth / (double)source.width
                       : (double)maxHeight / (double)source.height;

    int width  = (int)((double)source.width * scale);
    int height = (int)((double)source.height * scale);
    if (width < 1)  width = 1;
    if (height < 1) height = 1;

    out.width  = width;
    out.height = height;
    out.pixels.assign((size_t)width * (size_t)height * 4u, 0);

    const size_t sourceStride = (size_t)source.width * 4u;

    for (int y = 0; y < height; ++y)
    {
        // Each destination pixel averages the source box that maps onto it,
        // so no source pixel is skipped however large the reduction.
        const int y0 = (int)((long long)y * source.height / height);
        int       y1 = (int)((long long)(y + 1) * source.height / height);
        if (y1 <= y0) y1 = y0 + 1;

        for (int x = 0; x < width; ++x)
        {
            const int x0 = (int)((long long)x * source.width / width);
            int       x1 = (int)((long long)(x + 1) * source.width / width);
            if (x1 <= x0) x1 = x0 + 1;

            unsigned sum[4] = { 0, 0, 0, 0 };
            unsigned count  = 0;
            for (int sy = y0; sy < y1 && sy < source.height; ++sy)
            {
                const uint8_t* row = source.pixels.data() + (size_t)sy * sourceStride;
                for (int sx = x0; sx < x1 && sx < source.width; ++sx)
                {
                    const uint8_t* px = row + (size_t)sx * 4u;
                    sum[0] += px[0];
                    sum[1] += px[1];
                    sum[2] += px[2];
                    sum[3] += px[3];
                    ++count;
                }
            }
            if (count == 0)
                count = 1;

            uint8_t* dst = out.pixels.data() + ((size_t)y * (size_t)width + (size_t)x) * 4u;
            dst[0] = (uint8_t)(sum[0] / count);
            dst[1] = (uint8_t)(sum[1] / count);
            dst[2] = (uint8_t)(sum[2] / count);
            dst[3] = (uint8_t)(sum[3] / count);
        }
    }
    return true;
}
}
