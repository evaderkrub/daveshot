#include "platform/Autostart.h"
#include "platform/Clipboard.h"
#include "platform/ImageIo.h"
#include "platform/Paths.h"
#include "platform/PrintScreenKey.h"
#include "platform/SingleInstance.h"
#include "platform/macos/Bitmap.h"
#include "platform/macos/AppEvents.h"
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace daveshot::imageio {
const char* Extension(Format f) { return f == Format::Jpeg ? "jpg" : "png"; }
const char* FormatName(Format f) { return f == Format::Jpeg ? "JPEG" : "PNG"; }
bool Save(const std::string& path, const Image& image, Format format, int quality, std::string& error) {
    @autoreleasepool {
        NSBitmapImageRep* rep = macos::Bitmap(image);
        NSData* bytes = [rep representationUsingType:format == Format::Jpeg ? NSBitmapImageFileTypeJPEG : NSBitmapImageFileTypePNG
            properties:@{NSImageCompressionFactor: @(std::clamp(quality, 1, 100) / 100.0)}];
        NSError* failure = nil;
        if (bytes && [bytes writeToFile:[NSString stringWithUTF8String:path.c_str()] options:NSDataWritingAtomic error:&failure]) return true;
        error = failure ? failure.localizedDescription.UTF8String : "could not encode the image"; return false;
    }
}
bool Load(const std::string& path, Image& out, std::string& error) {
    @autoreleasepool {
        NSData* bytes = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:path.c_str()]];
        NSBitmapImageRep* rep = bytes ? [NSBitmapImageRep imageRepWithData:bytes] : nil;
        return macos::ReadBitmap(rep.CGImage, (int)rep.pixelsWide, (int)rep.pixelsHigh, out, error);
    }
}
}
namespace daveshot::clipboard {
bool CopyImage(const Image& image, std::string& error) {
    @autoreleasepool {
        NSData* bytes = [macos::Bitmap(image) representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        if (!bytes) { error = "could not encode the clipboard image"; return false; }
        NSPasteboard* paste = NSPasteboard.generalPasteboard;
        [paste clearContents];
        if ([paste setData:bytes forType:NSPasteboardTypePNG]) return true;
        error = "could not write to the clipboard"; return false;
    }
}
bool CopyText(const std::string& text, std::string& error) {
    @autoreleasepool {
        NSPasteboard* paste = NSPasteboard.generalPasteboard;
        [paste clearContents];
        if ([paste setString:[NSString stringWithUTF8String:text.c_str()] forType:NSPasteboardTypeString]) return true;
        error = "could not write to the clipboard"; return false;
    }
}
}
namespace daveshot::printkey {
Status Query() { return {}; }
bool Take(std::string& error) { error = "macOS does not have a Print Screen binding to take over"; return false; }
bool GiveBack(std::string& error) { return Take(error); }
}
namespace daveshot::autostart {
namespace {
NSString* LaunchPath() { return [NSHomeDirectory() stringByAppendingPathComponent:@"Library/LaunchAgents/org.daveshot.daveshot.plist"]; }
NSDictionary* Entry() { return [NSDictionary dictionaryWithContentsOfURL:[NSURL fileURLWithPath:LaunchPath()] error:nil]; }
}
State Query() {
    @autoreleasepool {
        NSArray* args = Entry()[@"ProgramArguments"];
        return args.count && [args[0] isEqualToString:[NSString stringWithUTF8String:paths::ExePath().c_str()]] ? State::On : State::Off;
    }
}
bool Enable(std::string& error) {
    @autoreleasepool {
        NSError* failure = nil;
        [[NSFileManager defaultManager] createDirectoryAtPath:LaunchPath().stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:&failure];
        NSDictionary* entry = @{@"Label": @"org.daveshot.daveshot", @"ProgramArguments": @[[NSString stringWithUTF8String:paths::ExePath().c_str()], @"--background"], @"RunAtLoad": @YES};
        if ([entry writeToURL:[NSURL fileURLWithPath:LaunchPath()] error:&failure]) return true;
        error = failure.localizedDescription.UTF8String; return false;
    }
}
bool Disable(std::string& error) {
    @autoreleasepool {
        if (Query() != State::On) return true;
        NSError* failure = nil;
        if ([[NSFileManager defaultManager] removeItemAtPath:LaunchPath() error:&failure]) return true;
        error = failure.localizedDescription.UTF8String; return false;
    }
}
const char* Label() { return "Start at login"; }
}
namespace daveshot::instance {
namespace { int lock = -1; id observer; bool wake = false; }
bool Claim() {
    @autoreleasepool {
        std::string path = paths::Beside("instance.lock");
        lock = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (lock < 0) return true;
        if (flock(lock, LOCK_EX | LOCK_NB) != 0) {
            const int failure = errno;
            close(lock); lock = -1;
            if (failure != EWOULDBLOCK) return true;
            [[NSDistributedNotificationCenter defaultCenter] postNotificationName:@"org.daveshot.daveshot.show" object:NSUserName() userInfo:nil deliverImmediately:YES];
            return false;
        }
        observer = [[NSDistributedNotificationCenter defaultCenter] addObserverForName:@"org.daveshot.daveshot.show" object:NSUserName() queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*) { wake = true; }];
        return true;
    }
}
void Release() {
    if (observer) { [[NSDistributedNotificationCenter defaultCenter] removeObserver:observer]; observer = nil; }
    if (lock >= 0) { close(lock); lock = -1; }
}
bool TakeWakeup() {
    const bool reopened = macos::TakeReopenRequest();
    const bool result = wake || reopened;
    wake = false;
    return result;
}
}
