#pragma once

namespace daveshot
{
    // Screen coordinates are physical pixels on the virtual desktop, whose
    // origin is the top-left of the primary monitor -- so x and y are
    // negative for monitors placed above or to the left of it. Nothing here
    // is in logical/DPI-scaled units; the capture path works in real pixels
    // from end to end.
    struct Rect
    {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;

        bool  Empty() const { return w <= 0 || h <= 0; }
        int   Right() const { return x + w; }
        int   Bottom() const { return y + h; }
        long long Area() const { return (long long)w * (long long)h; }

        bool Contains(int px, int py) const
        {
            return px >= x && py >= y && px < Right() && py < Bottom();
        }

        friend bool operator==(const Rect& a, const Rect& b)
        {
            return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
        }
        friend bool operator!=(const Rect& a, const Rect& b) { return !(a == b); }
    };

    // Builds a rect from two corners in any order, so a drag that runs right
    // to left or bottom to top still produces a positive-size rectangle.
    Rect RectFromCorners(int x0, int y0, int x1, int y1);

    // The overlapping part of two rects, or an empty rect when they do not
    // touch. Used to clip a selection to the desktop.
    Rect Intersect(const Rect& a, const Rect& b);
}
