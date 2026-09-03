#pragma once

#include <string>
#include <vector>

// The notification-area icon: what keeps daveshot reachable while its window
// is away. SDL supplies the icon and the menu on every platform, so this is
// one file rather than one per OS.
//
// The menu never does anything itself. Its entries queue an Action, and the
// frame loop -- the only thing holding the window and the screen -- acts on
// it, the same way a hotkey press or a panel button is handled.
namespace daveshot::tray
{
    enum class Action
    {
        Open,     // bring the window back
        Region,   // capture a region, as the hotkey would
        Screen,   // capture the full screen
        Quit,
    };

    // Puts the icon up. Fails, with a reason, where there is no notification
    // area to put it in -- a Linux desktop without an app-indicator library,
    // say -- and daveshot then behaves as it did before there was a tray:
    // closing the window quits.
    bool Create(std::string& error);
    void Destroy();
    bool Available();

    // Menu clicks since the last call, in order. Empty most frames.
    void Drain(std::vector<Action>& out);
}
