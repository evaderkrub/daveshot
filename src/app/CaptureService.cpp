#include "app/CaptureService.h"

#include "app/Naming.h"
#include "platform/Clipboard.h"
#include "platform/ImageIo.h"
#include "platform/Paths.h"
#include "platform/Screen.h"

#include <vector>

namespace daveshot::capture
{
namespace
{
    imageio::Format ToIoFormat(ImageFormat format)
    {
        return (format == ImageFormat::Jpeg) ? imageio::Format::Jpeg
                                             : imageio::Format::Png;
    }
}

std::string ResolveSaveFolder(const Settings& settings)
{
    if (!settings.saveFolder.empty())
        return settings.saveFolder;
    return JoinPath(paths::PicturesFolder(), "daveshot");
}

bool GrabDesktop(Image& out, Rect& bounds, std::string& error)
{
    bounds = screen::VirtualDesktopBounds();
    if (bounds.Empty())
    {
        error = "the desktop reported no size";
        return false;
    }
    return screen::CaptureRect(bounds, out, error);
}

bool Grab(const CaptureRequest& request, Image& out, Rect& source, std::string& error)
{
    switch (request.mode)
    {
    case CaptureMode::FullScreen:
    {
        source = screen::VirtualDesktopBounds();
        return screen::CaptureRect(source, out, error);
    }

    case CaptureMode::Monitor:
    {
        std::vector<screen::MonitorInfo> monitors;
        if (!screen::EnumerateMonitors(monitors, error))
            return false;

        // The list can shrink between the interface offering it and the
        // capture running -- a laptop being undocked is the ordinary way.
        // Falling back to the primary beats failing outright.
        size_t index = (size_t)(request.monitorIndex < 0 ? 0 : request.monitorIndex);
        if (index >= monitors.size())
            index = 0;

        source = monitors[index].bounds;
        return screen::CaptureRect(source, out, error);
    }

    case CaptureMode::Window:
    {
        if (request.windowHandle == 0)
        {
            error = "no window was chosen";
            return false;
        }
        if (!screen::CaptureWindow(request.windowHandle, out, error))
            return false;
        source = Rect{ 0, 0, out.width, out.height };
        return true;
    }

    case CaptureMode::Region:
        error = "a region capture goes through the selection overlay";
        return false;
    }

    error = "unknown capture mode";
    return false;
}

bool Save(Shot& shot, const Settings& settings, std::string& error)
{
    if (!shot.image.Valid())
    {
        error = "there is nothing to save";
        return false;
    }

    const std::string folder = ResolveSaveFolder(settings);
    if (!paths::EnsureFolder(folder, error))
        return false;

    const std::string stem = ExpandPattern(settings.filenamePattern, shot.when);
    const imageio::Format format = ToIoFormat(settings.format);
    const std::string path = UniquePath(folder, stem, imageio::Extension(format));

    if (!imageio::Save(path, shot.image, format,
                       ClampJpegQuality(settings.jpegQuality), error))
        return false;

    shot.savedPath = path;
    return true;
}

bool Copy(const Shot& shot, std::string& error)
{
    return clipboard::CopyImage(shot.image, error);
}
}
