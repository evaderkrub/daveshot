#include "platform/Clipboard.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace daveshot::clipboard
{
namespace
{
    // Opens the clipboard and guarantees it gets closed. Leaving it open
    // locks every other application out of it until this process exits.
    struct ClipboardScope
    {
        bool open = false;

        explicit ClipboardScope(HWND owner)
        {
            // The clipboard is briefly owned by whichever window is dragging
            // or menu-tracking, so a single attempt loses often enough to be
            // worth retrying.
            for (int attempt = 0; attempt < 5 && !open; ++attempt)
            {
                open = OpenClipboard(owner) != 0;
                if (!open)
                    Sleep(20);
            }
        }
        ~ClipboardScope()
        {
            if (open)
                CloseClipboard();
        }
    };

    // Packs the image into a DIB the way the clipboard wants it: a header
    // followed by pixels, bottom-up, BGRA.
    HGLOBAL BuildDibV5(const Image& image)
    {
        const size_t pixelBytes = (size_t)image.width * (size_t)image.height * 4u;
        const size_t total      = sizeof(BITMAPV5HEADER) + pixelBytes;

        HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE, total);
        if (block == nullptr)
            return nullptr;

        auto* bytes = static_cast<uint8_t*>(GlobalLock(block));
        if (bytes == nullptr)
        {
            GlobalFree(block);
            return nullptr;
        }

        auto* header = reinterpret_cast<BITMAPV5HEADER*>(bytes);
        ZeroMemory(header, sizeof(BITMAPV5HEADER));
        header->bV5Size        = sizeof(BITMAPV5HEADER);
        header->bV5Width       = image.width;
        header->bV5Height      = image.height;    // positive: bottom-up
        header->bV5Planes      = 1;
        header->bV5BitCount    = 32;
        header->bV5Compression = BI_BITFIELDS;
        header->bV5SizeImage   = (DWORD)pixelBytes;
        header->bV5RedMask     = 0x00FF0000;
        header->bV5GreenMask   = 0x0000FF00;
        header->bV5BlueMask    = 0x000000FF;
        header->bV5AlphaMask   = 0xFF000000;
        header->bV5CSType      = LCS_sRGB;
        header->bV5Intent      = LCS_GM_IMAGES;

        uint8_t* dst = bytes + sizeof(BITMAPV5HEADER);
        const size_t stride = (size_t)image.width * 4u;
        for (int row = 0; row < image.height; ++row)
        {
            // Flip: our images are top-down, a DIB with a positive height is
            // bottom-up.
            const uint8_t* src = image.pixels.data()
                               + (size_t)(image.height - 1 - row) * stride;
            uint8_t* out = dst + (size_t)row * stride;
            for (int col = 0; col < image.width; ++col)
            {
                out[col * 4 + 0] = src[col * 4 + 2];   // B
                out[col * 4 + 1] = src[col * 4 + 1];   // G
                out[col * 4 + 2] = src[col * 4 + 0];   // R
                out[col * 4 + 3] = src[col * 4 + 3];   // A
            }
        }

        GlobalUnlock(block);
        return block;
    }
}

bool CopyImage(const Image& image, std::string& error)
{
    if (!image.Valid())
    {
        error = "there is nothing to copy";
        return false;
    }

    ClipboardScope scope(nullptr);
    if (!scope.open)
    {
        error = "another application is holding the clipboard";
        return false;
    }

    HGLOBAL dib = BuildDibV5(image);
    if (dib == nullptr)
    {
        error = "not enough memory to copy the image";
        return false;
    }

    EmptyClipboard();

    // CF_DIBV5 carries the alpha channel; Windows synthesises the plain
    // CF_DIB that older paste targets ask for, so one format is enough.
    if (SetClipboardData(CF_DIBV5, dib) == nullptr)
    {
        GlobalFree(dib);
        error = "the clipboard refused the image";
        return false;
    }

    // Ownership passed to the clipboard on success -- freeing it here would
    // hand every pasting application a dangling block.
    return true;
}

bool CopyText(const std::string& text, std::string& error)
{
    ClipboardScope scope(nullptr);
    if (!scope.open)
    {
        error = "another application is holding the clipboard";
        return false;
    }

    const int count = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(),
                                          nullptr, 0);
    HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE, ((size_t)count + 1) * sizeof(wchar_t));
    if (block == nullptr)
    {
        error = "not enough memory to copy the text";
        return false;
    }

    auto* wide = static_cast<wchar_t*>(GlobalLock(block));
    if (count > 0)
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), wide, count);
    wide[count] = L'\0';
    GlobalUnlock(block);

    EmptyClipboard();
    if (SetClipboardData(CF_UNICODETEXT, block) == nullptr)
    {
        GlobalFree(block);
        error = "the clipboard refused the text";
        return false;
    }
    return true;
}
}
