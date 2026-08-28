#include "app/Capture.h"

#include <cstdio>

namespace daveshot
{
namespace
{
    unsigned gNextShotId = 1;
}

const char* CaptureModeName(CaptureMode mode)
{
    switch (mode)
    {
    case CaptureMode::Region:     return "Region";
    case CaptureMode::Window:     return "Window";
    case CaptureMode::Monitor:    return "Monitor";
    case CaptureMode::FullScreen: return "Full screen";
    }
    return "Capture";
}

Shot MakeShot(Image image, CaptureMode mode, const Rect& source, const TimeParts& when)
{
    Shot shot;
    shot.id     = gNextShotId++;
    shot.mode   = mode;
    shot.source = source;
    shot.when   = when;

    char label[96];
    std::snprintf(label, sizeof(label), "%s %dx%d",
                  CaptureModeName(mode), image.width, image.height);
    shot.label = label;

    ScaleImageToFit(image, kThumbnailSize, kThumbnailSize, shot.thumbnail);
    shot.image = std::move(image);
    return shot;
}

void History::Add(Shot shot)
{
    shots.insert(shots.begin(), std::move(shot));
    if (limit < 1)
        limit = 1;
    if ((int)shots.size() > limit)
        shots.resize((size_t)limit);
}

void History::Clear()
{
    shots.clear();
}

const Shot* History::Find(unsigned id) const
{
    for (const Shot& shot : shots)
        if (shot.id == id)
            return &shot;
    return nullptr;
}

Shot* History::Find(unsigned id)
{
    for (Shot& shot : shots)
        if (shot.id == id)
            return &shot;
    return nullptr;
}

size_t History::TotalBytes() const
{
    size_t total = 0;
    for (const Shot& shot : shots)
        total += shot.image.ByteSize() + shot.thumbnail.ByteSize();
    return total;
}
}
