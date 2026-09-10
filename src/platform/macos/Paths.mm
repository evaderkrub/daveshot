#include "platform/Paths.h"
#import <Cocoa/Cocoa.h>
#include <filesystem>
#include <mach-o/dyld.h>
#include <vector>

namespace daveshot::paths {
namespace { std::string exe, dir, data; }
bool Init(const char*, std::string& error) {
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        error = "could not locate the executable"; return false;
    }
    std::error_code ec;
    exe = std::filesystem::weakly_canonical(buffer.data(), ec).string();
    if (ec) { error = ec.message(); return false; }
    dir = std::filesystem::path(exe).parent_path().string();
    @autoreleasepool {
        NSString* support = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject;
        data = std::string(support.UTF8String) + "/daveshot";
    }
    return EnsureFolder(data, error);
}
const std::string& ExeDir() { return dir; }
const std::string& ExePath() { return exe; }
std::string Asset(const std::string& relative) {
    auto resources = std::filesystem::path(dir).parent_path() / "Resources" / "assets";
    if (std::filesystem::is_directory(resources)) return (resources / relative).string();
    return dir + "/assets/" + relative;
}
std::string Beside(const std::string& relative) { return data + "/" + relative; }
bool Exists(const std::string& path) { std::error_code ec; return std::filesystem::is_regular_file(path, ec); }
std::FILE* OpenFile(const std::string& path, const char* mode) { return std::fopen(path.c_str(), mode); }
bool RemoveFile(const std::string& path) { return std::remove(path.c_str()) == 0; }
std::string PicturesFolder() {
    @autoreleasepool { return NSSearchPathForDirectoriesInDomains(NSPicturesDirectory, NSUserDomainMask, YES).firstObject.UTF8String; }
}
bool EnsureFolder(const std::string& path, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) { error = "could not create " + path + ": " + ec.message(); return false; }
    return true;
}
bool RevealInFileBrowser(const std::string& path) {
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
        [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[url]];
        return true;
    }
}
}
