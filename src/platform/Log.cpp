#include "platform/Log.h"

#include "platform/Paths.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <ctime>
#include <string>

namespace daveshot::log
{
namespace
{
    constexpr long kMaxBytes = 1024 * 1024;

    std::string             gPath;
    SDL_LogOutputFunction   gPrevious = nullptr;
    void*                   gPreviousData = nullptr;

    void SDLCALL Output(void* userdata, int category, SDL_LogPriority priority,
                        const char* message)
    {
        if (gPrevious != nullptr)
            gPrevious(gPreviousData, category, priority, message);

        std::FILE* file = paths::OpenFile(gPath, "ab");
        if (file == nullptr)
            return;

        const std::time_t now = std::time(nullptr);
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
        std::fprintf(file, "%s %s\n", stamp, message != nullptr ? message : "");
        std::fclose(file);
        (void)userdata;
    }
}

void Install(const std::string& path)
{
    gPath = path;

    // Start over rather than grow without bound; a megabyte is months of
    // the few lines a day this writes.
    if (std::FILE* probe = paths::OpenFile(gPath, "rb"))
    {
        std::fseek(probe, 0, SEEK_END);
        const long size = std::ftell(probe);
        std::fclose(probe);
        if (size > kMaxBytes)
            paths::RemoveFile(gPath);
    }

    SDL_GetLogOutputFunction(&gPrevious, &gPreviousData);
    SDL_SetLogOutputFunction(Output, nullptr);
}
}
