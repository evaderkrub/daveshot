#include "platform/Host.h"

#include "daveshot/Version.h"
#include "platform/Paths.h"

#ifdef __linux__
#  include "platform/linux/ClipboardRetry.h"
#  include "platform/linux/ImageCodec.h"
#  include "platform/linux/Session.h"
#endif

#include <SDL3/SDL.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

namespace daveshot
{
Host::~Host()
{
    Shutdown();
}

bool Host::Startup(const char* title, int width, int height, bool visible,
                   std::string& error)
{
    // The identifier is what Wayland and the desktop portals know the
    // window by; on Windows it is only metadata.
#ifdef __linux__
    SDL_SetAppMetadata(kAppName, kAppVersion, session::kAppId);
#else
    SDL_SetAppMetadata(kAppName, kAppVersion, nullptr);
#endif

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        error = std::string("SDL_Init failed: ") + SDL_GetError();
        return false;
    }

    const SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
                                | (visible ? 0 : SDL_WINDOW_HIDDEN);
    if (!SDL_CreateWindowAndRenderer(title, width, height, flags, &m_window, &m_renderer))
    {
        error = std::string("could not create the window: ") + SDL_GetError();
        Shutdown();
        return false;
    }

    SDL_SetRenderVSync(m_renderer, 1);

#ifdef __linux__
    // Windows gets its icon from the resource compiled into the executable.
    // Here it is the PNG the same artwork was rendered to, set on the window
    // so the dock and the switcher have something other than a blank.
    {
        Image icon;
        std::string ignored;
        if (codec::DecodeFile(paths::Asset("icons/daveshot.png"), icon, ignored))
        {
            SDL_Surface* surface = SDL_CreateSurfaceFrom(icon.width, icon.height,
                                                         SDL_PIXELFORMAT_RGBA32,
                                                         icon.pixels.data(), icon.width * 4);
            if (surface != nullptr)
            {
                SDL_SetWindowIcon(m_window, surface);   // takes a copy
                SDL_DestroySurface(surface);
            }
        }
    }
#endif

    if (visible)
        SDL_ShowWindow(m_window);

#ifdef __linux__
    // The portals need to be able to name our window -- see Session.h. The
    // answer is read fresh on every call because it changes as the window is
    // shown and hidden, and it is wrong exactly when the window is down.
    session::SetParentWindowProvider([this] { return PortalParentWindow(); });
#endif

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable
                    | ImGuiConfigFlags_NavEnableKeyboard;

    // The layout file sits beside the executable, not in the working
    // directory, for the same reason every other path does.
    m_iniPath = paths::Beside("daveshot_layout.ini");
    io.IniFilename = m_iniPath.c_str();

    if (!ImGui_ImplSDL3_InitForSDLRenderer(m_window, m_renderer))
    {
        error = "could not initialise the ImGui SDL3 backend";
        Shutdown();
        return false;
    }
    if (!ImGui_ImplSDLRenderer3_Init(m_renderer))
    {
        error = "could not initialise the ImGui renderer backend";
        Shutdown();
        return false;
    }
    m_imguiBackendsUp = true;
    return true;
}

void Host::Shutdown()
{
    if (m_imguiBackendsUp)
    {
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        m_imguiBackendsUp = false;
    }
    if (ImGui::GetCurrentContext() != nullptr)
        ImGui::DestroyContext();

    if (m_renderer) { SDL_DestroyRenderer(m_renderer); m_renderer = nullptr; }
    if (m_window)   { SDL_DestroyWindow(m_window);     m_window   = nullptr; }
    SDL_Quit();
}

bool Host::PumpEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        ImGui_ImplSDL3_ProcessEvent(&event);
#ifdef __linux__
        clipboardretry::HandleEvent(event);
#endif

        if (event.type == SDL_EVENT_QUIT)
            m_running = false;
        else if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                 event.window.windowID == SDL_GetWindowID(m_window))
            m_closeRequested = true;
    }
#ifdef __linux__
    clipboardretry::Tick(m_window);
#endif
    return m_running;
}

bool Host::TakeCloseRequest()
{
    const bool requested = m_closeRequested;
    m_closeRequested = false;
    return requested;
}

void Host::WaitForEvent(int timeoutMs)
{
    // A null event leaves whatever arrived in the queue for PumpEvents to
    // take in its usual order.
    SDL_WaitEventTimeout(nullptr, timeoutMs);
}

void Host::BeginFrame()
{
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void Host::EndFrame(float clearR, float clearG, float clearB)
{
    ImGui::Render();

    SDL_SetRenderDrawColorFloat(m_renderer, clearR, clearG, clearB, 1.0f);
    SDL_RenderClear(m_renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), m_renderer);
    SDL_RenderPresent(m_renderer);
}

double Host::Now() const
{
    return (double)SDL_GetTicksNS() / 1e9;
}

#ifdef __linux__
std::string Host::PortalParentWindow() const
{
    if (m_window == nullptr)
        return std::string();

    const SDL_PropertiesID props = SDL_GetWindowProperties(m_window);
    if (session::IsWayland())
    {
        // SDL exports the toplevel through xdg_foreign when the window maps
        // and drops the export when it unmaps, so an empty answer here means
        // the window is genuinely not on screen.
        const char* handle = SDL_GetStringProperty(
            props, SDL_PROP_WINDOW_WAYLAND_XDG_TOPLEVEL_EXPORT_HANDLE_STRING, nullptr);
        if (handle == nullptr || *handle == '\0')
            return std::string();
        return std::string("wayland:") + handle;
    }

    const Uint64 xid = SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    if (xid == 0)
        return std::string();
    char text[32];
    SDL_snprintf(text, sizeof(text), "x11:%llx", (unsigned long long)xid);
    return text;
}
#endif

void Host::Hide()
{
    if (m_window)
        SDL_HideWindow(m_window);
}

void Host::Show()
{
    if (m_window == nullptr)
        return;
    SDL_ShowWindow(m_window);
    SDL_RaiseWindow(m_window);
}

bool Host::Hidden() const
{
    return m_window == nullptr || (SDL_GetWindowFlags(m_window) & SDL_WINDOW_HIDDEN) != 0;
}

bool Host::EnterOverlay(const Rect& desktop, Rect& covered, std::string& error)
{
    covered = desktop;
    if (m_window == nullptr)
    {
        error = "there is no window to put the overlay on";
        return false;
    }
    if (m_inOverlay)
        return true;

#ifdef __linux__
    if (session::IsWayland())
    {
        // Wayland gives a client no say over where its window goes and no
        // "always on top", and a window lives on one output. Fullscreen on
        // the display we are on is the one shape the compositor guarantees
        // to cover everything else; the caller crops the desktop capture to
        // that display.
        SDL_SetWindowFullscreenMode(m_window, nullptr);   // borderless, at the desktop's own mode
        SDL_SetWindowFullscreen(m_window, true);
        SDL_ShowWindow(m_window);
        SDL_SyncWindow(m_window);

        const Rect display = session::CaptureBoundsOfDisplay(SDL_GetDisplayForWindow(m_window));
        if (!display.Empty())
            covered = display;

        m_fullscreenOverlay = true;
        m_inOverlay = true;
        return true;
    }
#endif

    m_savedMaximised = (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MAXIMIZED) != 0;
    if (m_savedMaximised)
        SDL_RestoreWindow(m_window);
    SDL_GetWindowPosition(m_window, &m_savedGeometry.x, &m_savedGeometry.y);
    SDL_GetWindowSize(m_window, &m_savedGeometry.w, &m_savedGeometry.h);

    // Deliberately not SDL_SetWindowFullscreen: real fullscreen is per
    // display, and a region selection has to be able to cross monitors. A
    // borderless window sized to the whole virtual desktop can.
    SDL_SetWindowBordered(m_window, false);
    SDL_SetWindowResizable(m_window, false);
    SDL_SetWindowAlwaysOnTop(m_window, true);
    SDL_SetWindowPosition(m_window, desktop.x, desktop.y);
    SDL_SetWindowSize(m_window, desktop.w, desktop.h);
    SDL_ShowWindow(m_window);
    SDL_RaiseWindow(m_window);

    m_inOverlay = true;
    return true;
}

void Host::LeaveOverlay()
{
    if (!m_inOverlay || m_window == nullptr)
        return;

    if (m_fullscreenOverlay)
    {
        SDL_SetWindowFullscreen(m_window, false);
        SDL_SyncWindow(m_window);
        m_fullscreenOverlay = false;
        m_inOverlay = false;
        return;
    }

    SDL_SetWindowAlwaysOnTop(m_window, false);
    SDL_SetWindowBordered(m_window, true);
    SDL_SetWindowResizable(m_window, true);
    SDL_SetWindowSize(m_window, m_savedGeometry.w, m_savedGeometry.h);
    SDL_SetWindowPosition(m_window, m_savedGeometry.x, m_savedGeometry.y);
    if (m_savedMaximised)
        SDL_MaximizeWindow(m_window);

    m_inOverlay = false;
}

void Host::WindowPosition(int& x, int& y) const
{
    x = 0;
    y = 0;
    if (m_window)
        SDL_GetWindowPosition(m_window, &x, &y);
}

float Host::DisplayScale() const
{
    if (m_window == nullptr)
        return 1.0f;
    const float scale = SDL_GetWindowDisplayScale(m_window);
    return (scale > 0.0f) ? scale : 1.0f;
}

ImTextureID Host::CreateTexture(const Image& image)
{
    if (m_renderer == nullptr || !image.Valid())
        return 0;

    SDL_Texture* texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA32,
                                             SDL_TEXTUREACCESS_STATIC,
                                             image.width, image.height);
    if (texture == nullptr)
        return 0;

    if (!SDL_UpdateTexture(texture, nullptr, image.pixels.data(), image.width * 4))
    {
        SDL_DestroyTexture(texture);
        return 0;
    }

    // Previews and thumbnails are almost always drawn smaller than the
    // capture; nearest-neighbour would turn screenshot text into noise.
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    return (ImTextureID)(intptr_t)texture;
}

void Host::DestroyTexture(ImTextureID texture)
{
    if (texture != 0)
        SDL_DestroyTexture((SDL_Texture*)(intptr_t)texture);
}
}
