#include "platform/linux/ImageCodec.h"

#include "platform/Paths.h"

// Static: the ImGui test engine's capture tool carries its own copy of
// stb_image_write, and two external definitions of the same symbols would
// fail the link of anything that includes both.
#define STB_IMAGE_STATIC
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_WRITE_NO_STDIO
#include <stb_image.h>
#include <stb_image_write.h>

#include <cstdio>

namespace daveshot::codec
{
namespace
{
    void Collect(void* context, void* data, int size)
    {
        auto* out = static_cast<std::vector<uint8_t>*>(context);
        const auto* bytes = static_cast<const uint8_t*>(data);
        out->insert(out->end(), bytes, bytes + size);
    }
}

bool EncodePng(const Image& image, std::vector<uint8_t>& out, std::string& error)
{
    if (!image.Valid())
    {
        error = "there is nothing to encode";
        return false;
    }

    out.clear();
    // A screenshot is flat colour and text, which deflates well at any
    // level; the top level roughly doubles the time for a few percent.
    stbi_write_png_compression_level = 4;
    if (stbi_write_png_to_func(Collect, &out, image.width, image.height, 4,
                               image.pixels.data(), image.width * 4) == 0)
    {
        error = "PNG encoding failed";
        return false;
    }
    return true;
}

bool EncodeJpeg(const Image& image, int quality, std::vector<uint8_t>& out, std::string& error)
{
    if (!image.Valid())
    {
        error = "there is nothing to encode";
        return false;
    }
    if (quality < 1)   quality = 1;
    if (quality > 100) quality = 100;

    // JPEG has no alpha; hand the encoder three channels rather than
    // trusting it to skip the fourth.
    const size_t count = (size_t)image.width * (size_t)image.height;
    std::vector<uint8_t> rgb(count * 3);
    for (size_t i = 0; i < count; ++i)
    {
        rgb[i * 3 + 0] = image.pixels[i * 4 + 0];
        rgb[i * 3 + 1] = image.pixels[i * 4 + 1];
        rgb[i * 3 + 2] = image.pixels[i * 4 + 2];
    }

    out.clear();
    if (stbi_write_jpg_to_func(Collect, &out, image.width, image.height, 3,
                               rgb.data(), quality) == 0)
    {
        error = "JPEG encoding failed";
        return false;
    }
    return true;
}

bool DecodeFile(const std::string& path, Image& out, std::string& error)
{
    std::FILE* f = paths::OpenFile(path, "rb");
    if (f == nullptr)
    {
        error = "could not open " + path;
        return false;
    }

    std::vector<uint8_t> bytes;
    uint8_t chunk[1 << 16];
    for (;;)
    {
        const size_t got = std::fread(chunk, 1, sizeof(chunk), f);
        if (got == 0)
            break;
        bytes.insert(bytes.end(), chunk, chunk + got);
    }
    std::fclose(f);

    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), (int)bytes.size(),
                                            &width, &height, &channels, 4);
    if (pixels == nullptr)
    {
        error = "could not decode " + path + ": " + stbi_failure_reason();
        return false;
    }

    out.width  = width;
    out.height = height;
    out.pixels.assign(pixels, pixels + (size_t)width * (size_t)height * 4u);
    stbi_image_free(pixels);
    return true;
}
}
