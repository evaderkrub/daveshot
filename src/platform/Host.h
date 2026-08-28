#pragma once

#include <string>

struct SDL_Window;
struct SDL_Renderer;

namespace daveshot
{
    struct AppState;

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

        // Points at the requested UI scale so the window's own DPI change can
        // be folded in; see AppState::uiScale.
        float DisplayScale() const;

        SDL_Window*   Window()   const { return m_window; }
        SDL_Renderer* Renderer() const { return m_renderer; }

    private:
        SDL_Window*   m_window   = nullptr;
        SDL_Renderer* m_renderer = nullptr;
        bool          m_imguiBackendsUp = false;
        bool          m_running  = true;
        std::string   m_iniPath;   // ImGui keeps the pointer, so we own the storage
    };
}
