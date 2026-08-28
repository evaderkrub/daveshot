#pragma once

namespace daveshot
{
    struct AppState;

    namespace ui
    {
        // Draws one frame of the entire interface. Called between
        // ImGui::NewFrame() and ImGui::Render(), by the application and by the
        // end-to-end tests alike -- the tests drive this exact function, so
        // there is no second, test-only version of the interface to drift.
        //
        // Reads state and writes back into it. It does not own it.
        void Draw(AppState& state);

        // Window titles, in one place because the tests address them by name.
        inline constexpr const char* kWindowWorkspace = "Workspace";
        inline constexpr const char* kWindowSettings  = "Settings";
    }
}
