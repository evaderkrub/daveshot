#pragma once

namespace daveshot
{
    struct AppState;

    namespace ui
    {
        // Draws the About box. Always modal -- it is the one window that must
        // not be left open behind the main interface, because everything it
        // shows (version, build, paths) is what a user reads out when
        // reporting a problem, and a stale copy floating in a dock is worse
        // than none.
        //
        // Reads and clears state.showAbout, so the menu item only has to set
        // that flag.
        void DrawAboutDialog(AppState& state);
    }
}
