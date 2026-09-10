#include "platform/macos/AppEvents.h"
#import <Cocoa/Cocoa.h>

namespace { bool pendingReopen = false; }

@interface DaveshotReopenHandler : NSObject
- (void)handleReopen:(NSAppleEventDescriptor*)event reply:(NSAppleEventDescriptor*)reply;
@end

@implementation DaveshotReopenHandler
- (void)handleReopen:(NSAppleEventDescriptor*)event reply:(NSAppleEventDescriptor*)reply
{
    (void)event;
    (void)reply;
    // Let the application loop restore both the window and its in-tray state.
    pendingReopen = true;
}
@end

namespace daveshot::macos
{
namespace { DaveshotReopenHandler* handler; }
void InstallReopenHandler()
{
    if (handler) return;
    handler = [DaveshotReopenHandler new];
    [[NSAppleEventManager sharedAppleEventManager] setEventHandler:handler
        andSelector:@selector(handleReopen:reply:)
        forEventClass:kCoreEventClass andEventID:kAEReopenApplication];
}
void RemoveReopenHandler()
{
    if (!handler) return;
    [[NSAppleEventManager sharedAppleEventManager]
        removeEventHandlerForEventClass:kCoreEventClass andEventID:kAEReopenApplication];
    handler = nil;
    pendingReopen = false;
}
bool TakeReopenRequest()
{
    const bool requested = pendingReopen;
    pendingReopen = false;
    return requested;
}
}
