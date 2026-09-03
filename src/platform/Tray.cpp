#include "platform/Tray.h"

#include "app/Image.h"
#include "platform/ImageIo.h"
#include "platform/Paths.h"

#include <SDL3/SDL.h>

namespace daveshot::tray
{
namespace
{
    SDL_Tray*           gTray = nullptr;
    std::vector<Action> gPending;

    // SDL calls this from inside its event pump -- the tray's own window
    // procedure on Windows, GTK's main loop on Linux -- so it runs on the
    // main thread, between two of our frames, and a plain vector is enough.
    void SDLCALL OnEntry(void* userdata, SDL_TrayEntry*)
    {
        gPending.push_back((Action)(intptr_t)userdata);
    }

    void AddEntry(SDL_TrayMenu* menu, const char* label, Action action)
    {
        SDL_TrayEntry* entry = SDL_InsertTrayEntryAt(menu, -1, label, SDL_TRAYENTRY_BUTTON);
        if (entry != nullptr)
            SDL_SetTrayEntryCallback(entry, OnEntry, (void*)(intptr_t)action);
    }

    void AddSeparator(SDL_TrayMenu* menu)
    {
        SDL_InsertTrayEntryAt(menu, -1, nullptr, SDL_TRAYENTRY_BUTTON);
    }

    // The same artwork as the window icon, from the file the Linux build
    // reads it from; on Windows the executable's own icon resource is not
    // reachable as pixels without going back through the shell.
    bool LoadIcon(Image& icon, std::string& error)
    {
        Image full;
        if (!imageio::Load(paths::Asset("icons/daveshot.png"), full, error))
            return false;

#ifdef _WIN32
        // The shell scales an oversized icon with no filtering, and at the
        // 16 or 24 pixels the notification area draws, a 256-pixel source
        // comes out ragged. Averaging it down to something close first
        // leaves the shell with only a small step to take.
        return ScaleImageToFit(full, 32, 32, icon);
#else
        icon = std::move(full);
        return true;
#endif
    }
}

bool Create(std::string& error)
{
    if (gTray != nullptr)
        return true;

    Image icon;
    if (!LoadIcon(icon, error))
        return false;

    SDL_Surface* surface = SDL_CreateSurfaceFrom(icon.width, icon.height,
                                                 SDL_PIXELFORMAT_RGBA32,
                                                 icon.pixels.data(), icon.width * 4);
    if (surface == nullptr)
    {
        error = std::string("could not make the tray icon: ") + SDL_GetError();
        return false;
    }

    // SDL takes what it needs from the surface on the spot -- an HICON on
    // Windows, a file on Linux -- so the pixels can go once this returns.
    gTray = SDL_CreateTray(surface, "daveshot");
    SDL_DestroySurface(surface);
    if (gTray == nullptr)
    {
        error = std::string("no notification area: ") + SDL_GetError();
        return false;
    }

    SDL_TrayMenu* menu = SDL_CreateTrayMenu(gTray);
    if (menu == nullptr)
    {
        error = std::string("could not make the tray menu: ") + SDL_GetError();
        Destroy();
        return false;
    }

    AddEntry(menu, "Open daveshot", Action::Open);
    AddSeparator(menu);
    AddEntry(menu, "Capture region", Action::Region);
    AddEntry(menu, "Capture full screen", Action::Screen);
    AddSeparator(menu);
    AddEntry(menu, "Quit daveshot", Action::Quit);
    return true;
}

void Destroy()
{
    if (gTray != nullptr)
    {
        SDL_DestroyTray(gTray);
        gTray = nullptr;
    }
    gPending.clear();
}

bool Available()
{
    return gTray != nullptr;
}

void Drain(std::vector<Action>& out)
{
    out = gPending;
    gPending.clear();
}
}
