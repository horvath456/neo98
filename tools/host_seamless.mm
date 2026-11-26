// SPDX-License-Identifier: GPL-2.0-or-later
// The AppKit side of the seamless view; see tools/host_seamless.h and
//
#include "host_seamless.h"
#include "host_wheel.h"
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#include <SDL.h>
#include <SDL_syswm.h>

#include <deque>
#include <unordered_map>

namespace
{
// The top left corner of the guest's screen in AppKit's screen coordinates
// (which count from the bottom), set by Present from the anchor window.
double origin_x = 0, origin_top = 0;
std::deque<HostSeamless::MouseEvent> mouse_queue;
// A click makes its window the main window by itself; that is no choice in
// Mission Control.
NSTimeInterval last_press = 0;

// The buttons that went down in an item window and are not up yet.
unsigned held_buttons = 0;

void PushMouse(HostSeamless::MouseEvent::Kind kind, unsigned guest, NSEvent *source, int wheel = 0, int wheel_x = 0,
               unsigned wheel_flags = 0)
{
    HostSeamless::MouseEvent event;
    event.kind = kind;
    // Where the event happened, not where the pointer is by now.
    // An event without a window carries screen coordinates.
    const NSPoint at = source.window ? [source.window convertPointToScreen:source.locationInWindow]
                       : source ? source.locationInWindow : NSEvent.mouseLocation;
    event.x = (int)floor(at.x - origin_x);
    event.y = (int)floor(origin_top - at.y);
    event.buttons = held_buttons;
    event.window = guest;
    event.wheel = wheel;
    event.wheel_x = wheel_x;
    event.wheel_flags = wheel_flags;
    mouse_queue.push_back(event);
}
} // namespace

namespace
{
// The cursor of the Mac that stands for the guest's pointer, and whether the
// Mac pointer is over a window of the guest.
NSCursor *guest_cursor;
bool pointer_inside = false;
// A cursor AppKit has but does not publish (the wait ball, the diagonal
// size arrows, the question mark); `fallback` where this system lacks it.
NSCursor *Unpublished(NSString *name, NSCursor *fallback)
{
    const SEL selector = NSSelectorFromString(name);
    if (![NSCursor respondsToSelector:selector]) return fallback;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
    id cursor = nil;
    // Some of them answer the selector and then throw (_moveCursor).
    @try { cursor = [NSCursor performSelector:selector]; }
    @catch (NSException *) { cursor = nil; }
#pragma clang diagnostic pop
    return [cursor isKindOfClass:NSCursor.class] ? (NSCursor *)cursor : fallback;
}
// The guest's own picture as a cursor: one point per guest pixel, with a
// second representation of twice the pixels for a Retina screen, each pixel
// doubled so the picture stays sharp.
NSCursor *Picture(const HostSeamless::Cursor &cursor)
{
    const int w = cursor.width, h = cursor.height;
    if (w <= 0 || h <= 0 || cursor.argb.size() < size_t(w) * size_t(h)) return NSCursor.arrowCursor;
    NSImage *image = [[NSImage alloc] initWithSize:NSMakeSize(w, h)];
    for (int scale = 1; scale <= 2; ++scale)
    {
        NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
                                                                        pixelsWide:w * scale
                                                                        pixelsHigh:h * scale
                                                                     bitsPerSample:8
                                                                   samplesPerPixel:4
                                                                          hasAlpha:YES
                                                                          isPlanar:NO
                                                                    colorSpaceName:NSDeviceRGBColorSpace
                                                                       bytesPerRow:w * scale * 4
                                                                      bitsPerPixel:32];
        unsigned char *bytes = rep.bitmapData;
        for (int y = 0; y < h * scale; ++y)
            for (int x = 0; x < w * scale; ++x)
            {
                const unsigned p = cursor.argb[size_t(y / scale) * w + x / scale];
                unsigned char *q = bytes + (size_t(y) * w * scale + x) * 4;
                const unsigned a = p >> 24;
                q[0] = (unsigned char)(((p >> 16) & 255) * a / 255);
                q[1] = (unsigned char)(((p >> 8) & 255) * a / 255);
                q[2] = (unsigned char)((p & 255) * a / 255);
                q[3] = (unsigned char)a;
            }
        rep.size = NSMakeSize(w, h);
        [image addRepresentation:rep];
    }
    return [[NSCursor alloc] initWithImage:image hotSpot:NSMakePoint(cursor.hot_x, cursor.hot_y)];
}
NSCursor *MacCursor(const HostSeamless::Cursor &cursor)
{
    if (!cursor.shown)
    {
        static NSCursor *none;
        if (!none) none = [[NSCursor alloc] initWithImage:[[NSImage alloc] initWithSize:NSMakeSize(16, 16)] hotSpot:NSZeroPoint];
        return none;
    }
    switch (cursor.identifier)
    {
    case 32512: return NSCursor.arrowCursor;
    case 32513: return NSCursor.IBeamCursor;
    // The hourglass is the wait ball of the Mac; "working in background"
    // has no cursor of its own there and gets the same.
    case 32514: case 32650: return Unpublished(@"busyButClickableCursor", NSCursor.arrowCursor);
    case 32515: return NSCursor.crosshairCursor;
    case 32516: return NSCursor.resizeUpCursor;
    case 32642: return Unpublished(@"_windowResizeNorthWestSouthEastCursor", NSCursor.crosshairCursor);
    case 32643: return Unpublished(@"_windowResizeNorthEastSouthWestCursor", NSCursor.crosshairCursor);
    case 32644: return NSCursor.resizeLeftRightCursor;
    case 32645: return NSCursor.resizeUpDownCursor;
    case 32646: return NSCursor.openHandCursor;
    case 32648: return NSCursor.operationNotAllowedCursor;
    case 32649: return NSCursor.pointingHandCursor;
    case 32651: return Unpublished(@"_helpCursor", NSCursor.arrowCursor);
    default: return Picture(cursor);
    }
}
} // namespace

// One window of the guest on the Mac. It never takes the keyboard: the
// window of SDL keeps it and hands the keys to the guest as before.
@interface NEO98GuestWindow : NSWindow
@property unsigned guest;
@property BOOL listed;
@property unsigned long long pictureHash;
// The user took the window out of the Dock and the guest has not restored
// its window yet (since `restoringSince`); `expected`: the host itself takes
// it out because the guest restored the window.
@property BOOL restoring;
@property BOOL expected;
@property NSTimeInterval restoringSince;
@end

@implementation NEO98GuestWindow
- (BOOL)canBecomeKeyWindow
{
    return NO;
}
- (BOOL)canBecomeMainWindow
{
    return self.listed;
}
- (void)becomeMainWindow
{
    [super becomeMainWindow];
    if (NSProcessInfo.processInfo.systemUptime - last_press > 0.5)
        PushMouse(HostSeamless::MouseEvent::Chosen, self.guest, nil);
}
// A click on the minimized window in the Dock: the guest restores its window
// (SC_RESTORE), as by a click on its button in the taskbar.
- (void)leftDock:(NSNotification *)note
{
    if (self.expected) { self.expected = NO; return; }
    self.restoring = YES;
    self.restoringSince = NSProcessInfo.processInfo.systemUptime;
    PushMouse(HostSeamless::MouseEvent::Restore, self.guest, nil);
}
@end

@interface NEO98GuestView : NSView
{
    HostWheel _wheel;
    bool _begin;
}
@property unsigned guest;
@end

@implementation NEO98GuestView
- (BOOL)acceptsFirstMouse:(NSEvent *)event
{
    return YES;
}
- (void)updateTrackingAreas
{
    for (NSTrackingArea *area in self.trackingAreas.copy) [self removeTrackingArea:area];
    [self addTrackingArea:[[NSTrackingArea alloc]
                              initWithRect:NSZeroRect
                                   options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | NSTrackingCursorUpdate |
                                           NSTrackingActiveAlways | NSTrackingInVisibleRect
                                     owner:self
                                  userInfo:nil]];
    [super updateTrackingAreas];
}
- (void)button:(NSEvent *)event bit:(unsigned)bit down:(BOOL)down
{
    last_press = NSProcessInfo.processInfo.systemUptime;
    held_buttons = down ? (held_buttons | bit) : (held_buttons & ~bit);
    PushMouse(HostSeamless::MouseEvent::Button, self.guest, event);
}
- (void)mouseDown:(NSEvent *)event { [self button:event bit:1 down:YES]; }
- (void)mouseUp:(NSEvent *)event { [self button:event bit:1 down:NO]; }
- (void)rightMouseDown:(NSEvent *)event { [self button:event bit:2 down:YES]; }
- (void)rightMouseUp:(NSEvent *)event { [self button:event bit:2 down:NO]; }
- (void)otherMouseDown:(NSEvent *)event { [self button:event bit:4 down:YES]; }
- (void)otherMouseUp:(NSEvent *)event { [self button:event bit:4 down:NO]; }
- (void)mouseMoved:(NSEvent *)event
{
    pointer_inside = true;
    [guest_cursor set];
    PushMouse(HostSeamless::MouseEvent::Move, self.guest, event);
}
- (void)cursorUpdate:(NSEvent *)event
{
    [guest_cursor set];
}
- (void)mouseDragged:(NSEvent *)event { PushMouse(HostSeamless::MouseEvent::Move, self.guest, event); }
- (void)rightMouseDragged:(NSEvent *)event { PushMouse(HostSeamless::MouseEvent::Move, self.guest, event); }
- (void)otherMouseDragged:(NSEvent *)event { PushMouse(HostSeamless::MouseEvent::Move, self.guest, event); }
- (void)mouseEntered:(NSEvent *)event
{
    pointer_inside = true;
    [guest_cursor set];
    PushMouse(HostSeamless::MouseEvent::Move, self.guest, event);
}
- (void)mouseExited:(NSEvent *)event
{
    pointer_inside = false;
    [NSCursor.arrowCursor set];
    PushMouse(HostSeamless::MouseEvent::Leave, self.guest, event);
}
- (void)scrollWheel:(NSEvent *)event
{
    // A notch of a wheel, or a step of a trackpad with its phase: the
    // windows of the seamless view show the guest a pixel a point.
    if (event.phase == NSEventPhaseBegan || event.phase == NSEventPhaseMayBegin) _begin = true;
    HostScrollEvent seen;
    seen.precise = event.hasPreciseScrollingDeltas;
    seen.points_y = event.scrollingDeltaY;
    seen.points_x = event.scrollingDeltaX;
    seen.lines_y = event.deltaY;
    seen.lines_x = event.deltaX;
    seen.begin = _begin;
    seen.momentum = event.momentumPhase != NSEventPhaseNone;
    const HostScroll out = HostWheelFromView(_wheel, seen);
    if (seen.precise && (seen.points_y || seen.points_x)) _begin = false;
    if (!out.y && !out.x) return;
    PushMouse(HostSeamless::MouseEvent::Wheel, self.guest, event, out.y, out.x, out.flags);
}
@end

namespace
{
NSMutableDictionary<NSNumber *, NEO98GuestWindow *> *item_windows;

NSWindow *Native(SDL_Window *window)
{
    if (!window) return nil;
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_COCOA) return nil;
    return info.info.cocoa.window;
}
// The renderer of SDL draws into a CAMetalLayer of a view below the content
// view; every layer on the way must carry alpha for the desktop behind the
// window to show through.
void LayersOpaque(NSView *view, bool opaque)
{
    if (!view) return;
    if (view.layer) view.layer.opaque = opaque ? YES : NO;
    for (CALayer *layer in view.layer.sublayers) layer.opaque = opaque ? YES : NO;
    for (NSView *child in view.subviews) LayersOpaque(child, opaque);
}
} // namespace

namespace HostSeamless
{
bool Transparent(SDL_Window *window, bool on)
{
    @autoreleasepool
    {
        NSWindow *native = Native(window);
        if (!native) return false;
        native.opaque = on ? NO : YES;
        native.backgroundColor = on ? NSColor.clearColor : NSColor.blackColor;
        native.hasShadow = on ? NO : YES;
        LayersOpaque(native.contentView, !on);
        native.contentView.layer.backgroundColor = on ? nil : CGColorGetConstantColor(kCGColorBlack);
        if (!on) native.ignoresMouseEvents = NO;
        // The overlay is a window of the application like any other: it
        // takes part in Mission Control and stays out of the window cycle.
        native.collectionBehavior = on ? (NSWindowCollectionBehaviorManaged | NSWindowCollectionBehaviorIgnoresCycle)
                                       : NSWindowCollectionBehaviorDefault;
        [native invalidateShadow];
        return true;
    }
}

// What decides whether the desktop shows through the window, one line per
// view and layer, for a run in a real window (the step native=1).
static void DescribeLayer(CALayer *layer, int depth, std::string &out)
{
    char line[256];
    snprintf(line, sizeof line, "%*slayer %s opaque=%d background=%.2f hidden=%d frame=%.0fx%.0f\n", depth * 2, "",
             NSStringFromClass([layer class]).UTF8String, layer.opaque ? 1 : 0,
             layer.backgroundColor ? CGColorGetAlpha(layer.backgroundColor) : -1.0, layer.hidden ? 1 : 0,
             layer.frame.size.width, layer.frame.size.height);
    out += line;
    for (CALayer *child in layer.sublayers) DescribeLayer(child, depth + 1, out);
}
static void DescribeView(NSView *view, int depth, std::string &out)
{
    char line[256];
    snprintf(line, sizeof line, "%*sview %s opaque=%d layer=%s frame=%.0fx%.0f\n", depth * 2, "",
             NSStringFromClass([view class]).UTF8String, view.opaque ? 1 : 0,
             view.layer ? NSStringFromClass([view.layer class]).UTF8String : "-", view.frame.size.width, view.frame.size.height);
    out += line;
    for (NSView *child in view.subviews) DescribeView(child, depth + 1, out);
}
std::string Describe(SDL_Window *window)
{
    @autoreleasepool
    {
        NSWindow *native = Native(window);
        if (!native) return "no native window\n";
        std::string out;
        char line[320];
        snprintf(line, sizeof line,
                 "window number=%ld frame=%.0f,%.0f,%.0fx%.0f style=%lx opaque=%d background=%.2f shadow=%d level=%ld "
                 "visible=%d miniaturized=%d ignores-mouse=%d\n",
                 (long)native.windowNumber, native.frame.origin.x, native.frame.origin.y, native.frame.size.width,
                 native.frame.size.height, (unsigned long)native.styleMask, native.opaque ? 1 : 0,
                 native.backgroundColor.alphaComponent, native.hasShadow ? 1 : 0, (long)native.level,
                 native.visible ? 1 : 0, native.miniaturized ? 1 : 0, native.ignoresMouseEvents ? 1 : 0);
        out += line;
        // The frame view, then the content view with what lies below it.
        NSView *top = native.contentView;
        while (top.superview) top = top.superview;
        snprintf(line, sizeof line, "  frame view %s layer opaque=%d background=%.2f\n",
                 NSStringFromClass([top class]).UTF8String, top.layer.opaque ? 1 : 0,
                 top.layer.backgroundColor ? CGColorGetAlpha(top.layer.backgroundColor) : -1.0);
        out += line;
        DescribeView(native.contentView, 1, out);
        if (native.contentView.layer) DescribeLayer(native.contentView.layer, 1, out);
        // Every window of the application the window server shows.
        for (NSWindow *other in NSApp.windows)
        {
            if (!other.visible) continue;
            snprintf(line, sizeof line, "  shown number=%ld class=%s frame=%.0f,%.0f,%.0fx%.0f opaque=%d alpha=%.2f level=%ld title=%s\n",
                     (long)other.windowNumber, NSStringFromClass([other class]).UTF8String, other.frame.origin.x,
                     other.frame.origin.y, other.frame.size.width, other.frame.size.height, other.opaque ? 1 : 0,
                     other.alphaValue, (long)other.level, other.title.UTF8String ?: "");
            out += line;
        }
        return out;
    }
}

// SDL's content view paints its layer black whenever AppKit asks it to update
// (it does so for every window that was not created transparent, and the
// SDL2 interface cannot create one). The black lies behind the picture, so
// the host takes it away again before each picture.
void KeepClear(SDL_Window *window)
{
    @autoreleasepool
    {
        NSWindow *native = Native(window);
        CALayer *layer = native.contentView.layer;
        if (layer && layer.backgroundColor) layer.backgroundColor = nil;
    }
}

void ClickThrough(SDL_Window *window, bool on)
{
    @autoreleasepool
    {
        NSWindow *native = Native(window);
        if (native && native.ignoresMouseEvents != (on ? YES : NO)) native.ignoresMouseEvents = on ? YES : NO;
    }
}

bool Pointer(SDL_Window *window, int &x, int &y)
{
    @autoreleasepool
    {
        NSWindow *native = Native(window);
        if (!native) return false;
        const NSPoint screen = NSEvent.mouseLocation;
        const NSRect content = [native contentRectForFrameRect:native.frame];
        x = (int)floor(screen.x - content.origin.x);
        // AppKit counts from the bottom, the guest from the top.
        y = (int)floor(content.origin.y + content.size.height - screen.y);
        return true;
    }
}

void Present(SDL_Window *anchor, const std::vector<Item> &items, const unsigned char *pixels, int width, int height)
{
    @autoreleasepool
    {
        NSWindow *anchor_window = Native(anchor);
        if (!anchor_window) return;
        if (!item_windows) item_windows = [NSMutableDictionary dictionary];
        const NSRect content = [anchor_window contentRectForFrameRect:anchor_window.frame];
        origin_x = content.origin.x;
        origin_top = content.origin.y + content.size.height;
        const NSPoint mouse = NSEvent.mouseLocation;
        const int mouse_x = (int)floor(mouse.x - origin_x), mouse_y = (int)floor(origin_top - mouse.y);
        static CGColorSpaceRef colours = CGColorSpaceCreateDeviceRGB();
        static std::vector<unsigned> picture;
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        std::vector<NEO98GuestWindow *> order(items.size(), nil);
        std::vector<bool> made(items.size(), false);
        NSMutableSet<NSNumber *> *alive = [NSMutableSet set];
        for (size_t i = 0; i < items.size(); ++i)
        {
            const Item &item = items[i];
            NSNumber *key = @(item.id);
            if (!item.minimized) [alive addObject:key];
            const int w = item.frame.right - item.frame.left, h = item.frame.bottom - item.frame.top;
            const NSRect frame = NSMakeRect(origin_x + item.frame.left, origin_top - item.frame.bottom, w, h);
            NEO98GuestWindow *window = item_windows[key];
            if (item.minimized)
            {
                // Minimized in the guest: the Mac window goes into the Dock
                // with the picture it had. A window that started
                // minimized has none; the menu "Window" lists it.
                if (!window) continue;
                [alive addObject:key];
                NSString *title = [NSString stringWithUTF8String:item.title.c_str()] ?: @"";
                if (![window.title isEqualToString:title]) window.title = title;
                if (window.restoring && NSProcessInfo.processInfo.systemUptime - window.restoringSince > 3.0)
                    window.restoring = NO; // the guest did not restore it
                if (!window.miniaturized && !window.restoring) [window miniaturize:nil];
                continue;
            }
            if (!window)
            {
                window = [[NEO98GuestWindow alloc] initWithContentRect:frame
                                                             styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskMiniaturizable
                                                               backing:NSBackingStoreBuffered
                                                                 defer:NO];
                [NSNotificationCenter.defaultCenter addObserver:window
                                                       selector:@selector(leftDock:)
                                                           name:NSWindowDidDeminiaturizeNotification
                                                         object:window];
                window.guest = item.id;
                window.releasedWhenClosed = NO;
                window.opaque = NO;
                window.backgroundColor = NSColor.clearColor;
                window.hasShadow = NO;
                window.animationBehavior = NSWindowAnimationBehaviorNone;
                NEO98GuestView *view = [[NEO98GuestView alloc] initWithFrame:NSMakeRect(0, 0, w, h)];
                view.guest = item.id;
                view.wantsLayer = YES;
                view.layer.magnificationFilter = kCAFilterNearest;
                view.layer.contentsGravity = kCAGravityResize;
                window.contentView = view;
                item_windows[key] = window;
                made[i] = true;
            }
            order[i] = window;
            // The guest restored its window (its taskbar button, the menu
            // "Window"): the Mac window leaves the Dock.
            window.restoring = NO;
            if (window.miniaturized) { window.expected = YES; [window deminiaturize:nil]; }
            if (!NSEqualRects(window.frame, frame)) [window setFrame:frame display:YES];
            if (window.listed != (item.listed ? YES : NO) || made[i])
            {
                window.listed = item.listed ? YES : NO;
                window.excludedFromWindowsMenu = item.listed ? NO : YES;
                window.collectionBehavior = item.listed
                                                ? NSWindowCollectionBehaviorManaged | NSWindowCollectionBehaviorParticipatesInCycle
                                                : NSWindowCollectionBehaviorTransient | NSWindowCollectionBehaviorIgnoresCycle;
            }
            NSString *title = [NSString stringWithUTF8String:item.title.c_str()] ?: @"";
            if (![window.title isEqualToString:title]) window.title = title;
            // The mouse is this window's where the guest window is visible.
            bool takes = !item.through && item.frame.Holds(mouse_x, mouse_y);
            if (takes)
            {
                takes = false;
                for (const auto &r : item.rects)
                    if (r.Holds(mouse_x, mouse_y)) { takes = true; break; }
            }
            const BOOL ignores = item.through || (item.frame.Holds(mouse_x, mouse_y) && !takes) ? YES : NO;
            if (window.ignoresMouseEvents != ignores) window.ignoresMouseEvents = ignores;
            ComposeItem(item, pixels, width, height, picture);
            unsigned long long hash = 1469598103934665603ull ^ (unsigned long long)w * 65537u ^ (unsigned long long)h;
            for (unsigned value : picture) { hash ^= value; hash *= 1099511628211ull; }
            if (made[i] || hash != window.pictureHash)
            {
                window.pictureHash = hash;
                CFDataRef data = CFDataCreate(nullptr, (const UInt8 *)picture.data(), (CFIndex)(picture.size() * 4));
                CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
                CGImageRef image = CGImageCreate((size_t)w, (size_t)h, 8, 32, (size_t)w * 4, colours,
                                                 kCGBitmapByteOrder32Little | (CGBitmapInfo)kCGImageAlphaPremultipliedFirst,
                                                 provider, nullptr, false, kCGRenderingIntentDefault);
                window.contentView.layer.contents = (__bridge id)image;
                CGImageRelease(image);
                CGDataProviderRelease(provider);
                CFRelease(data);
            }
        }
        for (NSNumber *key in item_windows.allKeys)
            if (![alive containsObject:key])
            {
                [NSNotificationCenter.defaultCenter removeObserver:item_windows[key]];
                [item_windows[key] orderOut:nil];
                [item_windows[key] close];
                [item_windows removeObjectForKey:key];
            }
        // A new window stands in front of the guest window behind it; the
        // rearmost one of all comes to the front of the Mac's windows.
        for (size_t i = items.size(); i-- > 0;)
        {
            if (!made[i]) continue;
            // The icons of the desktop lie behind every window of the Mac,
            // on the Finder's desktop.
            if (items[i].desktop) { [order[i] orderBack:nil]; continue; }
            if (i + 1 < items.size() && !items[i + 1].desktop && order[i + 1])
                [order[i] orderWindow:NSWindowAbove relativeTo:order[i + 1].windowNumber];
            else
                [order[i] orderFrontRegardless];
        }
        // The order of the guest holds among its windows: one that is behind
        // a window the guest has behind it comes forward, to just in front
        // of that one. Mac windows in between stay where they are otherwise.
        NSArray<NSNumber *> *numbers = [NSWindow windowNumbersWithOptions:0];
        std::unordered_map<NSInteger, double> depth;
        for (NSUInteger n = 0; n < numbers.count; ++n) depth[numbers[n].integerValue] = (double)n;
        for (size_t i = items.size(); i-- > 1;)
        {
            if (items[i].desktop)
            {
                // A click brought the icons forward: back behind the Mac's
                // windows with them.
                if (numbers.count && numbers.lastObject.integerValue != order[i].windowNumber) [order[i] orderBack:nil];
                continue;
            }
            const auto behind = depth.find(order[i].windowNumber), front = depth.find(order[i - 1].windowNumber);
            if (behind == depth.end() || front == depth.end()) continue;
            if (front->second > behind->second)
            {
                [order[i - 1] orderWindow:NSWindowAbove relativeTo:order[i].windowNumber];
                front->second = behind->second - 0.5;
            }
        }
        [CATransaction commit];
    }
}

void CloseItems()
{
    @autoreleasepool
    {
        for (NSNumber *key in item_windows.allKeys)
        {
            [NSNotificationCenter.defaultCenter removeObserver:item_windows[key]];
            [item_windows[key] orderOut:nil];
            [item_windows[key] close];
        }
        [item_windows removeAllObjects];
        mouse_queue.clear();
        held_buttons = 0;
    }
}

void SetCursor(const Cursor &cursor)
{
    @autoreleasepool
    {
        if (!NSApp) return;
        guest_cursor = MacCursor(cursor);
        if (pointer_inside) [guest_cursor set];
    }
}

void ApplyCursor(bool over_guest)
{
    @autoreleasepool
    {
        if (!NSApp) return;
        pointer_inside = over_guest;
        if (over_guest) [guest_cursor ?: NSCursor.arrowCursor set];
        else [NSCursor.arrowCursor set];
    }
}

bool NextMouse(MouseEvent &event)
{
    if (mouse_queue.empty()) return false;
    event = mouse_queue.front();
    mouse_queue.pop_front();
    return true;
}

unsigned ItemWindows() { return (unsigned)item_windows.count; }

unsigned PostMouse(int x, int y, TestMouse what)
{
    @autoreleasepool
    {
        // The window that took the press keeps the mouse until the release,
        // as AppKit does it for a drag.
        static NSInteger pressed = 0;
        // The window the window server has at this point, whoever owns it.
        const NSPoint screen = NSMakePoint(origin_x + x + 0.5, origin_top - y - 0.5);
        NSInteger number = [NSWindow windowNumberAtPoint:screen belowWindowWithWindowNumber:0];
        // The windows of the pointer and of the drag frame take no mouse;
        // the window server looks through them, and so does this.
        for (bool again = true; again;)
        {
            again = false;
            for (NEO98GuestWindow *window in item_windows.allValues)
                if (window.windowNumber == number && window.ignoresMouseEvents && (window.guest == PointerItem || window.guest == OutlineItem))
                {
                    number = [NSWindow windowNumberAtPoint:screen belowWindowWithWindowNumber:number];
                    again = true;
                    break;
                }
        }
        const bool held = pressed && (what == TestMouse::Up || what == TestMouse::Move);
        if (held) number = pressed;
        for (NEO98GuestWindow *window in item_windows.allValues)
        {
            if (window.windowNumber != number || (window.ignoresMouseEvents && !held)) continue;
            NEO98GuestView *view = (NEO98GuestView *)window.contentView;
            if (what == TestMouse::WheelUp || what == TestMouse::WheelDown)
            {
                CGEventRef wheel = CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine, 1,
                                                                 what == TestMouse::WheelUp ? 1 : -1);
                // Quartz counts from the top of the first screen.
                CGEventSetLocation(wheel, CGPointMake(screen.x, NSScreen.screens[0].frame.size.height - screen.y));
                [view scrollWheel:[NSEvent eventWithCGEvent:wheel]];
                CFRelease(wheel);
                return window.guest;
            }
            const NSEventType type = what == TestMouse::Down ? NSEventTypeLeftMouseDown
                                     : what == TestMouse::Up ? NSEventTypeLeftMouseUp
                                     : held ? NSEventTypeLeftMouseDragged : NSEventTypeMouseMoved;
            NSEvent *event = [NSEvent mouseEventWithType:type
                                                location:[window convertPointFromScreen:screen]
                                           modifierFlags:0
                                               timestamp:NSProcessInfo.processInfo.systemUptime
                                            windowNumber:number
                                                 context:nil
                                             eventNumber:0
                                              clickCount:1
                                                pressure:what == TestMouse::Down || held ? 1 : 0];
            if (what == TestMouse::Down) pressed = number;
            if (what == TestMouse::Up) pressed = 0;
            // A move without a button reaches a view through its tracking
            // area, which only the window server feeds.
            if (type == NSEventTypeMouseMoved) [view mouseMoved:event];
            else [window sendEvent:event];
            return window.guest;
        }
        if (what == TestMouse::Up) pressed = 0;
        return 0;
    }
}

void RaiseItems(const std::vector<Item> &items)
{
    @autoreleasepool
    {
        for (size_t i = items.size(); i-- > 0;)
            if (!items[i].desktop && !items[i].minimized) [item_windows[@(items[i].id)] orderFrontRegardless];
    }
}

bool Active()
{
    @autoreleasepool
    {
        return NSApp && NSApp.active;
    }
}
} // namespace HostSeamless
