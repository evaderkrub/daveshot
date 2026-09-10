#include "platform/Screen.h"
#include "platform/macos/Bitmap.h"
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#include <algorithm>
#include <cmath>
#include <unistd.h>

namespace daveshot::screen {
namespace {
Rect Bounds(CGRect r) { return {(int)std::floor(r.origin.x), (int)std::floor(r.origin.y), (int)std::ceil(r.size.width), (int)std::ceil(r.size.height)}; }
// Keep desktop images in the same logical coordinate space as SDL windows.
// A Retina display therefore contributes one image pixel per desktop point.
SCShareableContent* Content(std::string& error) {
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    __block SCShareableContent* result = nil;
    __block NSError* failure = nil;
    [SCShareableContent getShareableContentExcludingDesktopWindows:YES onScreenWindowsOnly:YES
        completionHandler:^(SCShareableContent* content, NSError* e) {
            result = content; failure = e; dispatch_semaphore_signal(done);
        }];
    if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 15 * NSEC_PER_SEC))) {
        error = "macOS timed out while listing capture sources"; return nil;
    }
    if (!result) error = failure ? failure.localizedDescription.UTF8String : "no capture sources are available";
    return result;
}
bool Snapshot(SCContentFilter* filter, SCStreamConfiguration* config, Image& out, std::string& error) {
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    __block NSBitmapImageRep* result = nil;
    __block NSError* failure = nil;
    [SCScreenshotManager captureImageWithFilter:filter configuration:config completionHandler:^(CGImageRef image, NSError* e) {
        if (image) result = [[NSBitmapImageRep alloc] initWithCGImage:image];
        failure = e; dispatch_semaphore_signal(done);
    }];
    if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 15 * NSEC_PER_SEC))) {
        error = "macOS timed out while capturing the screen"; return false;
    }
    if (!result) { error = failure ? failure.localizedDescription.UTF8String : "macOS returned no screenshot"; return false; }
    return macos::ReadBitmap(result.CGImage, (int)config.width, (int)config.height, out, error);
}
}
Features Capabilities() { return {}; }
bool NeedsCapturePermission() { return !CGPreflightScreenCaptureAccess(); }
bool RequestCapturePermission(std::string& error) {
    if (CGPreflightScreenCaptureAccess() || CGRequestScreenCaptureAccess()) return true;
    error = "Allow daveshot in System Settings > Privacy & Security > Screen & System Audio Recording, then quit and reopen daveshot.";
    return false;
}
bool EnumerateMonitors(std::vector<MonitorInfo>& out, std::string& error) {
    out.clear(); uint32_t count = 0;
    if (CGGetActiveDisplayList(0, nullptr, &count) != kCGErrorSuccess || !count) { error = "no displays are available"; return false; }
    std::vector<CGDirectDisplayID> displays(count);
    if (CGGetActiveDisplayList(count, displays.data(), &count) != kCGErrorSuccess) { error = "could not list displays"; return false; }
    for (uint32_t i = 0; i < count; ++i) {
        Rect r = Bounds(CGDisplayBounds(displays[i]));
        out.push_back({"Monitor " + std::to_string(i + 1) + " (" + std::to_string(r.w) + "x" + std::to_string(r.h) + ")", r, displays[i] == CGMainDisplayID()});
    }
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.primary && !b.primary; });
    return true;
}
Rect VirtualDesktopBounds() {
    std::vector<MonitorInfo> monitors; std::string ignored;
    if (!EnumerateMonitors(monitors, ignored)) return {};
    CGRect all = CGRectNull;
    for (auto& m : monitors) all = CGRectUnion(all, CGRectMake(m.bounds.x, m.bounds.y, m.bounds.w, m.bounds.h));
    return Bounds(all);
}
bool EnumerateWindows(std::vector<WindowInfo>& out, std::string& error) {
    @autoreleasepool {
        out.clear();
        NSArray* windows = CFBridgingRelease(CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID));
        if (!windows) { error = "could not list windows"; return false; }
        for (NSDictionary* info in windows) {
            if ([info[(__bridge NSString*)kCGWindowOwnerPID] intValue] == getpid() || [info[(__bridge NSString*)kCGWindowLayer] intValue] != 0) continue;
            NSString* title = info[(__bridge NSString*)kCGWindowName];
            NSString* owner = info[(__bridge NSString*)kCGWindowOwnerName];
            if (!owner.length) continue;
            CGRect r;
            if (!CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)info[(__bridge NSString*)kCGWindowBounds], &r)) continue;
            if (r.size.width <= 0 || r.size.height <= 0) continue;
            out.push_back({[info[(__bridge NSString*)kCGWindowNumber] unsignedLongLongValue], (title.length ? title : owner).UTF8String, owner.UTF8String, Bounds(r)});
        }
        return true;
    }
}
bool CaptureRect(const Rect& area, Image& out, std::string& error) {
    @autoreleasepool {
        if (!RequestCapturePermission(error)) return false;
        Rect clipped = Intersect(area, VirtualDesktopBounds());
        if (clipped.Empty()) { error = "the capture area is outside the desktop"; return false; }
        SCShareableContent* content = Content(error);
        if (!content) return false;
        Image result{clipped.w, clipped.h, std::vector<uint8_t>((size_t)clipped.w * (size_t)clipped.h * 4, 0)};
        for (size_t i = 3; i < result.pixels.size(); i += 4) result.pixels[i] = 255;
        for (SCDisplay* display in content.displays) {
            Rect bounds = Bounds(display.frame);
            Rect part = Intersect(clipped, bounds);
            if (part.Empty()) continue;
            SCContentFilter* filter = [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];
            SCStreamConfiguration* config = [SCStreamConfiguration new];
            config.width = part.w; config.height = part.h; config.showsCursor = NO;
            config.sourceRect = CGRectMake(part.x - bounds.x, part.y - bounds.y, part.w, part.h);
            Image image;
            if (!Snapshot(filter, config, image, error)) return false;
            for (int y = 0; y < part.h; ++y)
                std::memcpy(result.pixels.data() + ((size_t)(part.y - clipped.y + y) * clipped.w + part.x - clipped.x) * 4,
                    image.pixels.data() + (size_t)y * part.w * 4, (size_t)part.w * 4);
        }
        out = std::move(result); return true;
    }
}
bool CaptureWindow(uint64_t handle, Image& out, std::string& error) {
    @autoreleasepool {
        if (!RequestCapturePermission(error)) return false;
        SCShareableContent* content = Content(error);
        if (!content) return false;
        for (SCWindow* window in content.windows) if (window.windowID == handle) {
            SCContentFilter* filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:window];
            SCStreamConfiguration* config = [SCStreamConfiguration new];
            config.width = (size_t)std::ceil(window.frame.size.width);
            config.height = (size_t)std::ceil(window.frame.size.height);
            config.showsCursor = NO; config.ignoreShadowsSingleWindow = YES;
            return Snapshot(filter, config, out, error);
        }
        error = "the selected window is no longer available"; return false;
    }
}
uint64_t WindowAtPoint(int x, int y) {
    std::vector<WindowInfo> windows; std::string error;
    EnumerateWindows(windows, error);
    for (auto& w : windows) if (x >= w.bounds.x && y >= w.bounds.y && x < w.bounds.x + w.bounds.w && y < w.bounds.y + w.bounds.h) return w.handle;
    return 0;
}
}
