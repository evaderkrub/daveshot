#pragma once

namespace daveshot::macos
{
    // Install after SDL creates NSApplication, preserving SDL's delegate and
    // its quit/file-open handling. Dock clicks send a reopen Apple Event even
    // when the app is already active, so activation notifications are not enough.
    void InstallReopenHandler();
    void RemoveReopenHandler();
    bool TakeReopenRequest();
}
