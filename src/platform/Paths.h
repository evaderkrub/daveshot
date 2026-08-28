#pragma once

#include <cstdio>
#include <string>

// Every runtime path is resolved against the executable's own directory. The
// working directory is whatever the shortcut, debugger or drag-and-drop
// happened to set, and relying on it is the usual way a portable build stops
// being portable.
namespace daveshot::paths
{
    // Must be called once at startup, before any other function here.
    // argv0 is a fallback for platforms where the OS query fails.
    bool Init(const char* argv0, std::string& error);

    // Directory containing the running executable, without a trailing slash.
    const std::string& ExeDir();

    // ExeDir() + "/assets/" + relative.  e.g. Asset("fonts/OpenSans-Regular.ttf")
    std::string Asset(const std::string& relative);

    // ExeDir() + "/" + relative. For files the application writes: settings,
    // logs, the imgui layout .ini.
    std::string Beside(const std::string& relative);

    bool Exists(const std::string& path);

    // Opens a UTF-8 path. Everything above this line produces UTF-8, and the
    // CRT's narrow fopen() interprets its argument in the machine's ANSI code
    // page -- so a user whose profile folder contains a non-ASCII character
    // would get "file not found" for a file that is plainly there. On Windows
    // this converts and calls _wfopen instead. Returns nullptr on failure.
    std::FILE* OpenFile(const std::string& path, const char* mode);

    bool RemoveFile(const std::string& path);
}
