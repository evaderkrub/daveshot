#pragma once

#include <SDL3/SDL.h>

// The frame loop's side of the Linux clipboard: see Clipboard.cpp for why a
// copy is not always accepted the first time. Host calls these; nothing
// else does.
namespace daveshot::clipboardretry
{
    void HandleEvent(const SDL_Event& event);
    void Tick(SDL_Window* window);
}
