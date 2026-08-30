#include "platform/ImageIo.h"

#include "platform/Paths.h"
#include "platform/linux/ImageCodec.h"

#include <cstdio>
#include <vector>

namespace daveshot::imageio
{
const char* Extension(Format format)
{
    return (format == Format::Jpeg) ? "jpg" : "png";
}

const char* FormatName(Format format)
{
    return (format == Format::Jpeg) ? "JPEG" : "PNG";
}

bool Save(const std::string& path, const Image& image,
          Format format, int quality, std::string& error)
{
    std::vector<uint8_t> encoded;
    const bool ok = (format == Format::Jpeg)
                  ? codec::EncodeJpeg(image, quality, encoded, error)
                  : codec::EncodePng(image, encoded, error);
    if (!ok)
        return false;

    std::FILE* f = paths::OpenFile(path, "wb");
    if (f == nullptr)
    {
        error = "could not create " + path;
        return false;
    }

    const bool written = std::fwrite(encoded.data(), 1, encoded.size(), f) == encoded.size();
    const bool closed  = std::fclose(f) == 0;
    if (!written || !closed)
    {
        error = "could not write " + path;
        paths::RemoveFile(path);   // a half-written image is worse than none
        return false;
    }
    return true;
}
}
