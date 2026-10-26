#pragma once

#include <string>

// Where SDL_Log lines end up. A desktop application has no console, so by
// default they go to the debugger and nowhere else -- and a thing that goes
// wrong once an afternoon, at a moment nobody is watching, needs somewhere
// it can be read back from afterwards.
namespace daveshot::log
{
    // Appends every SDL_Log line, with a timestamp, to the file at `path`,
    // on top of wherever SDL was already sending it. A file that has grown
    // past about a megabyte is started over. Safe to call before the window
    // exists.
    void Install(const std::string& path);
}
