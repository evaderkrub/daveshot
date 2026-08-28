// Entry point. Everything else lives under src/app, src/ui and src/platform.
//
// SDL_main.h is what makes a GUI-subsystem build work: on Windows it supplies
// the WinMain that SDL needs and forwards to the main() below.
#include <SDL3/SDL_main.h>

#include "ui/AppLoop.h"

int main(int argc, char** argv)
{
    return daveshot::ui::RunApplication(argc, argv);
}
