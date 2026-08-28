#pragma once

#include "ui/Panels.h"

namespace daveshot
{
    struct AppState;

    namespace ui
    {
        class TextureCache;

        // Draws one frame of the entire interface. Called between
        // ImGui::NewFrame() and ImGui::Render(), by the application and by the
        // end-to-end tests alike -- the tests drive this exact function, so
        // there is no second, test-only version of the interface to drift
        // away from the real one.
        //
        // Reads state and writes back into it. It does not own it, and it
        // never touches the screen: a capture button raises a flag that the
        // frame loop acts on.
        void Draw(AppState& state, TextureCache& textures);

        // Kept for the panels and the tests: the workspace was replaced by
        // the capture and preview panels.
        inline constexpr const char* kWindowWorkspace = kWindowPreview;
    }
}
