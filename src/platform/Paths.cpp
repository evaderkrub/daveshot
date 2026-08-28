#include "platform/Paths.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <shlobj.h>
#else
#  include <limits.h>
#  include <unistd.h>
#endif

namespace daveshot::paths
{
namespace
{
    std::string gExeDir;

#ifdef _WIN32
    std::wstring Utf8ToWide(const std::string& utf8)
    {
        if (utf8.empty())
            return std::wstring();
        const int count = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(),
                                              nullptr, 0);
        std::wstring wide((size_t)(count > 0 ? count : 0), 0);
        if (count > 0)
            MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(),
                                wide.data(), count);
        return wide;
    }
#endif

    void StripFilename(std::string& path)
    {
        const size_t cut = path.find_last_of("/\\");
        path = (cut == std::string::npos) ? std::string(".") : path.substr(0, cut);
    }

#ifdef _WIN32
    // GetModuleFileNameW truncates rather than failing when the buffer is too
    // small, so grow until it fits instead of trusting MAX_PATH -- long paths
    // are legal on modern Windows and a truncated result would silently point
    // at the wrong folder.
    bool QueryExePath(std::string& out)
    {
        std::wstring wide(MAX_PATH, L'\0');
        for (;;)
        {
            const DWORD written = GetModuleFileNameW(nullptr, wide.data(), (DWORD)wide.size());
            if (written == 0)
                return false;
            if (written < wide.size())
            {
                wide.resize(written);
                break;
            }
            wide.resize(wide.size() * 2);
        }

        const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(),
                                              nullptr, 0, nullptr, nullptr);
        if (bytes <= 0)
            return false;
        out.resize((size_t)bytes);
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(),
                            out.data(), bytes, nullptr, nullptr);
        return true;
    }
#else
    bool QueryExePath(std::string& out)
    {
        char buffer[PATH_MAX];
        const ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
        if (len <= 0)
            return false;
        buffer[len] = '\0';
        out.assign(buffer, (size_t)len);
        return true;
    }
#endif
}

bool Init(const char* argv0, std::string& error)
{
    std::string path;
    if (!QueryExePath(path))
    {
        if (argv0 == nullptr || *argv0 == '\0')
        {
            error = "could not determine the executable's location";
            return false;
        }
        path = argv0;
    }

    StripFilename(path);
    gExeDir = path;
    return true;
}

const std::string& ExeDir()
{
    return gExeDir;
}

std::string Asset(const std::string& relative)
{
    return gExeDir + "/assets/" + relative;
}

std::string Beside(const std::string& relative)
{
    return gExeDir + "/" + relative;
}

bool Exists(const std::string& path)
{
    std::FILE* f = OpenFile(path, "rb");
    if (f == nullptr)
        return false;
    std::fclose(f);
    return true;
}

std::FILE* OpenFile(const std::string& path, const char* mode)
{
#ifdef _WIN32
    // _wfopen_s rather than _wfopen: the plain form is deprecated under
    // /W4, and the checked form is what the CRT wants us to call.
    std::FILE* f = nullptr;
    if (_wfopen_s(&f, Utf8ToWide(path).c_str(), Utf8ToWide(mode).c_str()) != 0)
        return nullptr;
    return f;
#else
    return std::fopen(path.c_str(), mode);
#endif
}

bool RemoveFile(const std::string& path)
{
#ifdef _WIN32
    return _wremove(Utf8ToWide(path).c_str()) == 0;
#else
    return std::remove(path.c_str()) == 0;
#endif
}
}

namespace daveshot::paths
{
std::string PicturesFolder()
{
#ifdef _WIN32
    PWSTR    wide = nullptr;
    std::string result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &wide)))
    {
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
        if (bytes > 1)
        {
            result.resize((size_t)bytes - 1);
            WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), bytes, nullptr, nullptr);
        }
    }
    if (wide != nullptr)
        CoTaskMemFree(wide);

    // No Pictures folder at all is exotic but not impossible (a stripped
    // service account). Falling back beside the executable keeps captures
    // going somewhere the user can find.
    if (result.empty())
        return ExeDir();
    return result;
#else
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/Pictures" : ExeDir();
#endif
}

bool EnsureFolder(const std::string& path, std::string& error)
{
    if (path.empty())
    {
        error = "no folder was given";
        return false;
    }

#ifdef _WIN32
    std::wstring wide = Utf8ToWide(path);
    // SHCreateDirectoryExW builds the whole chain and takes backslashes only.
    for (wchar_t& c : wide)
        if (c == L'/')
            c = L'\\';

    const int result = SHCreateDirectoryExW(nullptr, wide.c_str(), nullptr);
    if (result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS ||
        result == ERROR_FILE_EXISTS)
        return true;

    error = "could not create " + path;
    return false;
#else
    error = "creating folders is not implemented on this platform";
    return false;
#endif
}

bool RevealInFileBrowser(const std::string& path)
{
#ifdef _WIN32
    const std::wstring wide = Utf8ToWide(path);
    // ILCreateFromPath + SHOpenFolderAndSelectItems selects the file rather
    // than merely opening its folder, which is what "show it to me" means.
    PIDLIST_ABSOLUTE item = ILCreateFromPathW(wide.c_str());
    if (item == nullptr)
        return false;

    const HRESULT hr = SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
    ILFree(item);
    return SUCCEEDED(hr);
#else
    (void)path;
    return false;
#endif
}
}
