#include "platform/Host.h"

#include "platform/Paths.h"

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

bool Host::Startup(const char* title, int width, int height, std::string& error)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        error = std::string("SDL_Init failed: ") + SDL_GetError();
        return false;
    }

    const SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (!SDL_CreateWindowAndRenderer(title, width, height, flags, &m_window, &m_renderer))
    {
        error = std::string("could not create the window: ") + SDL_GetError();
        Shutdown();
        return false;
    }

    SDL_SetRenderVSync(m_renderer, 1);
    SDL_ShowWindow(m_window);

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

        if (event.type == SDL_EVENT_QUIT)
            m_running = false;
        else if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                 event.window.windowID == SDL_GetWindowID(m_window))
            m_running = false;
    }
    return m_running;
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

float Host::DisplayScale() const
{
    if (m_window == nullptr)
        return 1.0f;
    const float scale = SDL_GetWindowDisplayScale(m_window);
    return (scale > 0.0f) ? scale : 1.0f;
}
}
