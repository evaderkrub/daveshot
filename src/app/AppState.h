#pragma once

#include "app/Settings.h"

#include <string>

namespace daveshot
{
    // The whole of the application's state. The drawing code reads this and
    // writes back into it; it does not own it, and nothing here knows that
    // ImGui exists -- which is what lets the tests build a state, run the
    // rules over it, and check the result without a window.
    struct AppState
    {
        Settings settings;

        // --- Window visibility ------------------------------------------
        bool showAbout    = false;   // always opened as a modal, see AboutDialog
        bool showSettings = true;
        bool showDemo     = false;   // ImGui's own demo, handy while building

        // --- Transient ---------------------------------------------------
        bool        quitRequested = false;
        std::string status = "Ready";

        // Set when something failed in a place that could not show a dialog
        // itself. The UI drains it into a modal. Empty means no error.
        std::string error;

        // Bumped when uiScale or theme changes, so the frame loop knows to
        // rebuild the style and font atlas. Applying either mid-frame gives
        // inconsistent metrics for the rest of that frame.
        unsigned styleRevision = 1;
        unsigned appliedStyleRevision = 0;

        // Where settings.txt lives. Resolved from the executable directory at
        // startup; empty in tests, which do not persist anything.
        std::string settingsPath;
    };

    // The two mutators the UI uses. They exist here rather than inline in the
    // UI so the "changing this invalidates the style" rule has one home.
    void SetUiScale(AppState& state, float scale);
    void SetTheme(AppState& state, const std::string& themeName);

    bool StyleNeedsRebuild(const AppState& state);
    void MarkStyleApplied(AppState& state);

    void ReportError(AppState& state, const std::string& message);
}
