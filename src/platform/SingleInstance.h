#pragma once

#include <string>

// One daveshot at a time.
//
// An application that lives in the tray gets launched again: from the Start
// menu, from a shortcut, by the login item while a copy is already running.
// A second copy would find every hotkey taken and sit there with nothing to
// do, so instead it asks the first copy to show its window and goes away.
namespace daveshot::instance
{
    // Claims the slot. Returns false when another daveshot already holds
    // it, after asking that one to come forward -- the caller should then
    // exit quietly. A failure to claim for any other reason counts as a
    // claim: refusing to start over a broken lock would be worse than the
    // occasional second copy.
    bool Claim();
    void Release();

    // True once for each later launch that asked us to come forward since
    // the last call.
    bool TakeWakeup();
}
