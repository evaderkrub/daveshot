#pragma once

namespace daveshot::ui
{
    // The composition root: opens the window, loads settings and fonts,
    // runs the frame loop, saves settings on the way out. Returns the
    // process exit code.
    //
    // It lives under src/ui because it exists to drive the drawing; main.cpp
    // stays a one-line entry point, and src/app stays free of ImGui.
    int RunApplication(int argc, char** argv);
}
