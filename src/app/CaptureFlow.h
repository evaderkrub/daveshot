#pragma once

#include "app/AppState.h"

#include <string>

namespace daveshot
{
    // The things a capture needs done to the window and the screen. Behind an
    // interface so the sequencing below can be tested with a fake -- the
    // rules about when the window hides, how long the desktop gets to
    // repaint, and what happens when a grab fails are worth testing, and none
    // of them need a real screen to be worth testing on.
    class CaptureEffects
    {
    public:
        virtual ~CaptureEffects() = default;

        virtual void HideWindow() = 0;
        virtual void ShowWindow() = 0;

        virtual bool EnterOverlay(const Rect& desktop, std::string& error) = 0;
        virtual void LeaveOverlay() = 0;

        virtual bool Grab(const CaptureRequest& request, Image& out, Rect& source,
                          std::string& error) = 0;
        virtual bool GrabDesktop(Image& out, Rect& bounds, std::string& error) = 0;
    };

    // How long the desktop gets to redraw after our window hides. Hiding is
    // asynchronous -- the compositor still has our pixels on screen for a
    // frame or two -- and capturing too early puts daveshot's own window in
    // the shot, which is the one thing a screenshot tool must never do.
    inline constexpr double kHideSettleSeconds = 0.22;

    // Starts a capture. Ignored while one is already running: a second
    // PrintScreen during a countdown should not queue a second shot.
    void RequestCapture(AppState& state, const CaptureRequest& request, double now);

    // Moves the sequence along. Call once a frame with the app clock.
    void TickCapture(AppState& state, CaptureEffects& effects, double now);

    // Abandons a capture in progress -- Escape from the overlay, or a failed
    // grab. Always restores the window, whatever phase it was in.
    void CancelCapture(AppState& state, CaptureEffects& effects);

    // Turns the overlay's selection into a shot. Does nothing if the
    // selection is too small to be a deliberate drag, which is what a stray
    // click on the overlay looks like.
    void CommitSelection(AppState& state, CaptureEffects& effects);

    // Smallest drag, in pixels on either axis, that counts as a selection.
    inline constexpr int kMinSelection = 4;

    // Saves and copies a finished shot according to the settings, appending
    // what it did to state.status. Failures land in state.error.
    void ApplyPostCapture(AppState& state, Shot& shot);
}
