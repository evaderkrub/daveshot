#pragma once

#include "app/Geometry.h"
#include "app/Image.h"

#include "imgui.h"

#include <string>

struct SDL_Window;
struct SDL_Renderer;

namespace daveshot
{
    // Owns the window, the renderer and the ImGui backends -- the parts that
    // talk to the OS. Nothing above this file includes SDL.
    //
    // Failures come back as false plus an error string rather than an
    // exception, so the caller can put the message in front of the user.
    class Host
    {
    public:
        Host() = default;
        ~Host();

        Host(const Host&)            = delete;
        Host& operator=(const Host&) = delete;

        bool Startup(const char* title, int width, int height, std::string& error);
        void Shutdown();

        // Drains the OS event queue. Returns false when the user asked to quit.
        bool PumpEvents();

        void BeginFrame();
        void EndFrame(float clearR, float clearG, float clearB);

        // Seconds since startup, monotonic. The capture sequence times its
        // steps against this rather than against frame counts, because frames
        // stop arriving while the window is hidden.
        double Now() const;

        // --- Window ---------------------------------------------------------
        void Hide();
        void Show();

#ifdef __linux__
        // The window in the form a desktop portal's `parent_window` argument
        // takes, or empty while the window is hidden. See Session.h for why
        // the portals care.
        std::string PortalParentWindow() const;
#endif

        // Borderless, always on top, covering the whole virtual desktop: the
        // region-selection overlay. Remembers the ordinary window's geometry
        // so LeaveOverlay puts it back exactly.
        //
        // `covered` comes back as the part of the desktop the overlay really
        // occupies. That is all of it wherever a window can be placed across
        // monitors; on Wayland it is the one display the window is on.
        bool EnterOverlay(const Rect& desktop, Rect& covered, std::string& error);
        void LeaveOverlay();
        bool InOverlay() const { return m_inOverlay; }

        // Top-left of the window in desktop coordinates. The overlay adds
        // this to ImGui's window-relative mouse position to get a screen
        // coordinate.
        void WindowPosition(int& x, int& y) const;

        // The display's own scale factor, folded into the UI scale so 150% in
        // Windows and 1.5x here compound the way a user expects.
        float DisplayScale() const;

        // --- Textures -------------------------------------------------------
        // Uploads an RGBA image. Returns 0 on failure. The caller owns the
        // result and must hand it back to DestroyTexture.
        ImTextureID CreateTexture(const Image& image);
        void        DestroyTexture(ImTextureID texture);

        SDL_Window*   Window()   const { return m_window; }
        SDL_Renderer* Renderer() const { return m_renderer; }

    private:
        SDL_Window*   m_window   = nullptr;
        SDL_Renderer* m_renderer = nullptr;
        bool          m_imguiBackendsUp = false;
        bool          m_running  = true;
        std::string   m_iniPath;   // ImGui keeps the pointer, so we own the storage

        bool m_inOverlay = false;
        bool m_fullscreenOverlay = false;   // the Wayland form: one display, fullscreen
        Rect m_savedGeometry;      // window position and size before the overlay
        bool m_savedMaximised = false;
    };
}
