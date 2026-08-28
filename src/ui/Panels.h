#pragma once

namespace daveshot
{
    struct AppState;

    namespace ui
    {
        class TextureCache;

        // Each panel reads the state and writes back into it. None of them
        // takes a capture themselves -- they set fields on AppState, and the
        // capture sequence in app/CaptureFlow acts on those between frames.
        void DrawCapturePanel(AppState& state);
        void DrawPreviewPanel(AppState& state, TextureCache& textures);
        void DrawHistoryPanel(AppState& state, TextureCache& textures);
        void DrawSettingsPanel(AppState& state);

        // The full-screen region selector, drawn over the frozen desktop
        // while a region capture is in progress. Returns true when the user
        // finished a drag, so the caller can turn the selection into a shot.
        struct OverlayResult
        {
            bool committed = false;
            bool cancelled = false;
        };
        OverlayResult DrawRegionOverlay(AppState& state, TextureCache& textures);

        // Window titles, in one place because the tests address them by name.
        inline constexpr const char* kWindowCapture  = "Capture";
        inline constexpr const char* kWindowPreview  = "Preview";
        inline constexpr const char* kWindowHistory  = "History";
        inline constexpr const char* kWindowSettings = "Settings";
    }
}
