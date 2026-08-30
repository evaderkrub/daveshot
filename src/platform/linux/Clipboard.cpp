#include "platform/Clipboard.h"

#include "platform/linux/ClipboardRetry.h"
#include "platform/linux/ImageCodec.h"

#include <SDL3/SDL.h>

#include <cstring>
#include <vector>

namespace daveshot::clipboard
{
namespace
{
    // The clipboard on X11 and Wayland is not a place data is put; it is a
    // promise to hand data over when someone pastes. SDL keeps that promise
    // for us -- and it works on both display servers -- so the encoded PNG
    // is held here until the next copy replaces it or the process exits.
    // (Which is also the limit: after we quit, the promise is gone, unless
    // a clipboard manager took a copy.)
    std::vector<uint8_t> gPng;

    const void* SDLCALL Provide(void*, const char* mimeType, size_t* size)
    {
        if (mimeType != nullptr && std::strcmp(mimeType, "image/png") == 0)
        {
            *size = gPng.size();
            return gPng.data();
        }
        *size = 0;
        return nullptr;
    }

    void SDLCALL Cleanup(void*) {}

    constexpr const char* kImageMime = "image/png";

    // Mutter -- and it is not alone -- only lets the client that holds
    // keyboard focus set the selection, and silently cancels anyone else's
    // offer. A copy made while the capture overlay is coming down can land
    // in exactly that gap. Rather than report "copied" and hand the user an
    // empty clipboard, the offer is remembered and made again once the
    // window has focus, until the compositor confirms it (see Tick).
    enum class Pending { None, Image, Text };
    Pending     gPending = Pending::None;
    std::string gText;
    int         gAttempts = 0;
    uint64_t    gDeadlineMs = 0;

    constexpr int      kMaxAttempts = 4;
    constexpr uint64_t kGiveUpAfterMs = 15000;

    bool Offer()
    {
        ++gAttempts;
        if (gPending == Pending::Image)
        {
            const char* mimeTypes[] = { kImageMime };
            return SDL_SetClipboardData(Provide, Cleanup, nullptr, mimeTypes, 1);
        }
        if (gPending == Pending::Text)
            return SDL_SetClipboardText(gText.c_str());
        return true;
    }

    void Begin(Pending kind)
    {
        gPending    = kind;
        gAttempts   = 0;
        gDeadlineMs = SDL_GetTicks() + kGiveUpAfterMs;
    }
}

bool CopyImage(const Image& image, std::string& error)
{
    if (!image.Valid())
    {
        error = "there is nothing to copy";
        return false;
    }
    if (!SDL_WasInit(SDL_INIT_VIDEO))
    {
        error = "the clipboard is not available before the window exists";
        return false;
    }

    std::vector<uint8_t> encoded;
    if (!codec::EncodePng(image, encoded, error))
        return false;
    gPng.swap(encoded);

    // PNG is the one image format every paste target on Linux understands;
    // offering a BMP alongside would only add a second encode nobody asks for.
    Begin(Pending::Image);
    if (!Offer())
    {
        gPending = Pending::None;
        error = std::string("the clipboard refused the image: ") + SDL_GetError();
        return false;
    }
    return true;
}

bool CopyText(const std::string& text, std::string& error)
{
    if (!SDL_WasInit(SDL_INIT_VIDEO))
    {
        error = "the clipboard is not available before the window exists";
        return false;
    }
    gText = text;
    Begin(Pending::Text);
    if (!Offer())
    {
        gPending = Pending::None;
        error = std::string("the clipboard refused the text: ") + SDL_GetError();
        return false;
    }
    return true;
}
}

namespace daveshot::clipboardretry
{
void HandleEvent(const SDL_Event& event)
{
    // The compositor telling us the selection changed, and that we are the
    // owner, is the confirmation that our offer stood.
    if (event.type == SDL_EVENT_CLIPBOARD_UPDATE && event.clipboard.owner)
        clipboard::gPending = clipboard::Pending::None;
}

void Tick(SDL_Window* window)
{
    using namespace clipboard;
    if (gPending == Pending::None || window == nullptr)
        return;

    if (gAttempts >= kMaxAttempts || SDL_GetTicks() > gDeadlineMs)
    {
        // Enough. Retrying forever would eventually overwrite something the
        // user copied elsewhere in the meantime.
        gPending = Pending::None;
        return;
    }

    // A cancelled offer shows up as SDL no longer holding our data. Only
    // re-offer with focus, since without it the same thing happens again.
    const char* mime = (gPending == Pending::Image) ? kImageMime : "text/plain;charset=utf-8";
    const bool held    = SDL_HasClipboardData(mime);
    const bool focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    if (!held && focused)
        Offer();
}
}
