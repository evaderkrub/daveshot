#include "platform/Clipboard.h"

#include <SDL3/SDL.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace daveshot::clipboard
{
namespace
{
    // The clipboard records who put the data there. Owning it as our window
    // rather than as nobody (a null owner) is what the documentation asks
    // for, and it is also what lets a copy be checked on afterwards: if the
    // owner is no longer us, somebody else has written since.
    HWND OwnerWindow()
    {
        HWND hwnd = nullptr;
        int count = 0;
        SDL_Window** windows = SDL_GetWindows(&count);
        if (windows != nullptr)
        {
            if (count > 0)
                hwnd = static_cast<HWND>(SDL_GetPointerProperty(
                    SDL_GetWindowProperties(windows[0]),
                    SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
            SDL_free(windows);
        }
        return hwnd;
    }

    // Opens the clipboard and guarantees it gets closed. Leaving it open
    // locks every other application out of it until this process exits.
    struct ClipboardScope
    {
        bool open = false;

        explicit ClipboardScope(HWND owner)
        {
            // The clipboard is briefly owned by whichever window is dragging
            // or menu-tracking, and held for longer by whatever reads each
            // new item -- the clipboard history, a sync client, an office
            // suite's paste preview. A single attempt loses often enough to
            // be worth retrying, and a short retry loses to the readers.
            for (int attempt = 0; attempt < 10 && !open; ++attempt)
            {
                open = OpenClipboard(owner) != 0;
                if (!open)
                    Sleep(30);
            }
        }
        ~ClipboardScope()
        {
            if (open)
                CloseClipboard();
        }
    };

    // What the last copy left behind, so the loop can notice when another
    // program replaces it a moment later -- which, to the person pasting,
    // is indistinguishable from the copy never having happened.
    struct Watch
    {
        HWND      owner    = nullptr;
        DWORD     sequence = 0;
        ULONGLONG until    = 0;      // GetTickCount64 deadline; 0 = off
    };
    Watch gWatch;

    constexpr ULONGLONG kWatchMs = 5000;

    void ArmWatch(HWND owner, const char* what)
    {
        gWatch.owner    = owner;
        gWatch.sequence = GetClipboardSequenceNumber();
        gWatch.until    = GetTickCount64() + kWatchMs;
        SDL_Log("daveshot: clipboard: %s copied (owner %p, sequence %lu)",
                what, (void*)owner, (unsigned long)gWatch.sequence);
    }

    std::string ProcessNameOfWindow(HWND hwnd)
    {
        DWORD pid = 0;
        if (hwnd == nullptr || GetWindowThreadProcessId(hwnd, &pid) == 0 || pid == 0)
            return std::string();

        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (process == nullptr)
            return std::string();

        wchar_t path[MAX_PATH];
        DWORD   length = MAX_PATH;
        std::string name;
        if (QueryFullProcessImageNameW(process, 0, path, &length) != 0)
        {
            const wchar_t* base = path;
            for (const wchar_t* p = path; *p != L'\0'; ++p)
                if (*p == L'\\' || *p == L'/')
                    base = p + 1;

            const int bytes = WideCharToMultiByte(CP_UTF8, 0, base, -1, nullptr, 0, nullptr, nullptr);
            if (bytes > 1)
            {
                name.resize((size_t)bytes - 1);
                WideCharToMultiByte(CP_UTF8, 0, base, -1, name.data(), bytes, nullptr, nullptr);
            }
        }
        CloseHandle(process);
        return name;
    }

    // Says which window has the clipboard open right now, when it can be
    // told: the one piece of evidence worth having when opening it fails.
    std::string HolderDescription()
    {
        const HWND holder = GetOpenClipboardWindow();
        if (holder == nullptr)
            return "no window reports holding it";
        const std::string name = ProcessNameOfWindow(holder);
        return name.empty() ? "held by an unnamed window" : "held by " + name;
    }

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

    const HWND owner = OwnerWindow();
    ClipboardScope scope(owner);
    if (!scope.open)
    {
        error = "another application is holding the clipboard (" + HolderDescription() + ")";
        SDL_Log("daveshot: clipboard: image copy failed: %s", error.c_str());
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
        SDL_Log("daveshot: clipboard: image copy failed: %s", error.c_str());
        return false;
    }

    // Ownership passed to the clipboard on success -- freeing it here would
    // hand every pasting application a dangling block.
    ArmWatch(owner, "image");
    return true;
}

bool CopyText(const std::string& text, std::string& error)
{
    const HWND owner = OwnerWindow();
    ClipboardScope scope(owner);
    if (!scope.open)
    {
        error = "another application is holding the clipboard (" + HolderDescription() + ")";
        SDL_Log("daveshot: clipboard: text copy failed: %s", error.c_str());
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
        SDL_Log("daveshot: clipboard: text copy failed: %s", error.c_str());
        return false;
    }
    ArmWatch(owner, "text");
    return true;
}

std::string TakeOverwriteNotice()
{
    if (gWatch.until == 0)
        return std::string();

    // The sequence number moves on every change; reading it does not touch
    // the clipboard itself, so this is safe to ask every frame.
    const DWORD sequence = GetClipboardSequenceNumber();
    if (sequence == gWatch.sequence)
    {
        if (GetTickCount64() >= gWatch.until)
            gWatch.until = 0;
        return std::string();
    }

    // One notice per copy: a later change is somebody's ordinary copying.
    gWatch.until = 0;

    // Our own copy again -- the loop copying twice, or ImGui putting text
    // there through SDL -- keeps our window as owner and is not news.
    const HWND owner = GetClipboardOwner();
    if (owner != nullptr && owner == gWatch.owner)
        return std::string();

    const std::string who = ProcessNameOfWindow(owner);
    SDL_Log("daveshot: clipboard: overwritten after our copy by %s (owner %p, sequence %lu -> %lu)",
            who.empty() ? "an unknown program" : who.c_str(), (void*)owner,
            (unsigned long)gWatch.sequence, (unsigned long)sequence);

    std::string notice = "The clipboard was then overwritten by ";
    notice += who.empty() ? "another program" : who;
    return notice;
}
}
