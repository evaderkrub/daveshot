#pragma once

#include "app/Capture.h"
#include "app/Settings.h"
#include "platform/Autostart.h"
#include "platform/PrintScreenKey.h"
#include "platform/Screen.h"

#include <string>
#include <vector>

namespace daveshot
{
    // Where a capture is in its sequence. A capture is not one call: the
    // window has to get out of the way, the desktop has to be given a moment
    // to repaint without it, a countdown may have to run, and a region
    // capture then hands control to the overlay. Each of those is a frame
    // boundary, so the sequence is a state machine rather than a function.
    enum class CapturePhase
    {
        Idle,
        Countdown,     // the user asked for a delay; waiting it out
        Hiding,        // window hidden, waiting for the desktop to repaint
        Selecting,     // region overlay is up, user is dragging
        Finishing,     // shot taken, window coming back
    };

    // The whole of the application's state. The drawing code reads this and
    // writes back into it; it does not own it, and nothing here knows that
    // ImGui exists -- which is what lets the tests build a state, run the
    // rules over it, and check the result without a window.
    struct AppState
    {
        Settings settings;

        // --- Captures ------------------------------------------------------
        History        history;
        unsigned       selectedShotId = 0;   // 0 means "the newest"
        CaptureRequest request;              // what the capture buttons will do

        CapturePhase phase        = CapturePhase::Idle;
        double       phaseUntil   = 0.0;     // seconds, on the app clock
        double       countdownEnd = 0.0;     // for the "3.. 2.. 1" readout

        // The frozen desktop the region overlay draws and crops out of, and
        // where on the virtual desktop its top-left corner sits.
        Image backdrop;
        Rect  backdropBounds;

        // Selection in backdrop image space. Screen coordinates are this plus
        // backdropBounds' origin.
        bool selecting     = false;
        int  selectAnchorX = 0;
        int  selectAnchorY = 0;
        Rect selection;

        // --- Pickers -------------------------------------------------------
        std::vector<screen::MonitorInfo> monitors;
        std::vector<screen::WindowInfo>  windows;
        bool windowListStale = true;

        // --- The Print Screen key -------------------------------------------
        // What the desktop's own screenshot tool is doing with Print, so the
        // settings panel can offer to take the key over. The panel never asks
        // the OS itself: reading it runs a registry query or a subprocess, so
        // the loop does it when this goes stale and the panel reads the answer.
        printkey::Status printKey;
        bool             printKeyStale = true;

        // --- Running in the background -------------------------------------
        // Whether there is a notification area to put the window away in.
        // The loop finds out once at startup; without one, closing the
        // window quits, as it always did.
        bool trayAvailable = false;

        // True while the window has been put away. A capture that starts
        // from here ends here -- see Finish in CaptureFlow -- and a problem
        // that needs showing brings the window back, because the error
        // modal has nowhere else to be.
        bool inTray = false;

        // Whether this executable starts at login, cached the same way the
        // Print Screen answer is: reading it touches the registry or the
        // filesystem, so the loop does it when this goes stale.
        autostart::State autostart      = autostart::State::Unsupported;
        bool             autostartStale = true;

        // --- Window visibility ---------------------------------------------
        bool showAbout    = false;   // always opened as a modal, see AboutDialog
        bool showSettings = true;
        bool showHistory  = true;
        bool showDemo     = false;

        // True until the docking layout has been arranged. Set false at
        // startup when a saved layout exists, so a user's own arrangement is
        // never stomped by the default one.
        bool layoutPending = true;

        // --- Intent raised by the interface, acted on by the frame loop ----
        // The panels never take a capture themselves: they set these, and the
        // loop -- which is the only thing holding the window and the screen
        // -- does the work between frames. That is what keeps every panel
        // testable without a screen.
        bool pendingRequest   = false;
        bool pendingSelection = false;
        bool cancelRequested  = false;
        bool saveRequested    = false;
        bool copyRequested    = false;
        bool copyPathRequested = false;
        bool revealRequested  = false;

        // Taking Print Screen from the desktop, and handing it back. Raised
        // by the settings panel, acted on by the loop -- it changes a system
        // setting and then has to re-register the hotkeys behind it.
        bool takePrintKeyRequested = false;
        bool givePrintKeyRequested = false;

        // The window coming back from the tray, or going there. Raised by
        // the tray menu, the File menu and the close button; the loop moves
        // the window and keeps inTray in step.
        bool openRequested    = false;
        bool putAwayRequested = false;

        // Ticking or unticking "start at login". Raised by the settings
        // panel with the value wanted; the loop registers with the OS.
        bool autostartChangeRequested = false;
        bool autostartWanted          = false;

        // --- Transient -----------------------------------------------------
        bool        quitRequested = false;
        std::string status = "Ready";

        // Set when something failed in a place that could not show a dialog
        // itself. The UI drains it into a modal. Empty means no error.
        std::string error;

        // Bumped when uiScale or theme changes, so the frame loop knows to
        // rebuild the style and font atlas. Applying either mid-frame gives
        // inconsistent metrics for the rest of that frame.
        unsigned styleRevision        = 1;
        unsigned appliedStyleRevision = 0;

        // Raised when a hotkey setting changes, so the loop re-registers.
        unsigned hotkeyRevision        = 1;
        unsigned appliedHotkeyRevision = 0;

        // Where settings.txt lives. Resolved from the executable directory at
        // startup; empty in tests, which do not persist anything.
        std::string settingsPath;
    };

    // The mutators the UI uses. They live here rather than inline in the UI so
    // that "changing this invalidates that" has one home.
    void SetUiScale(AppState& state, float scale);
    void SetTheme(AppState& state, const std::string& themeName);
    void SetHotkeys(AppState& state, const std::string& region, const std::string& screen,
                    bool enabled);

    // Asks the loop to register the hotkeys again without changing them --
    // what a combination the OS has just released needs.
    void RefreshHotkeys(AppState& state);

    bool StyleNeedsRebuild(const AppState& state);
    void MarkStyleApplied(AppState& state);
    bool HotkeysNeedRegistering(const AppState& state);
    void MarkHotkeysApplied(AppState& state);

    void ReportError(AppState& state, const std::string& message);

    // The shot the preview is showing: the selected one, or the newest when
    // nothing is selected. Null when nothing has been captured yet.
    const Shot* CurrentShot(const AppState& state);
    Shot*       CurrentShot(AppState& state);

    // Records a finished capture, selects it, and returns a reference to it.
    Shot& AcceptShot(AppState& state, Shot shot);

    // Whether a capture is in flight. The interface hides itself and stops
    // accepting new capture requests while one is.
    bool CaptureInProgress(const AppState& state);

    // Seconds left on the countdown, rounded up for display. Zero when there
    // is no countdown running.
    int CountdownRemaining(const AppState& state, double now);
}
