// SPDX-License-Identifier: GPL-2.0-or-later
// The Dock icon of one program of the guest; see tools/host_dock.h. This
// process owns no window and runs no guest code: it shows the bundle's icon
// and name in the Dock and in Cmd+Tab and tells the host (its parent) what
// the user does with them. The host's end of the socket pair is file
// descriptor 3.
//
// neo98-dock-helper --channel 3 [--silent]
//
// --silent: no Dock icon and no activation (NSApplicationActivationPolicy
// Prohibited), for the tests of the channel; NEO98_DOCK_HELPER_SCRIPT=
// "seconds:message[@Name];..." then sends those messages by itself, with a
// name only the helper of the program of that name.
#import <AppKit/AppKit.h>
#include <unistd.h>

static int channel = 3;
static BOOL silent = NO;

static void Say(const char *message)
{
    char line[64];
    const int n = snprintf(line, sizeof line, "%s\n", message);
    if (write(channel, line, (size_t)n) != n) exit(0); // the host is gone
}

// What the user did with the icon, one report per choice.
//
// A click on the Dock icon arrives as two things, the activation and the
// reopen event, in either order: they are one choice and one line for the
// host. Cmd+Tab is the activation alone.
//
// The fuse: this application is active only for the moment between the
// user's choice and the host taking over, so a row of activations is not the
// user. After FuseChoices reports within FuseSeconds the helper says "storm"
// once, reports nothing and hands nothing over until FuseRest seconds passed
// without an activation. A loop of activations between the helpers and the
// host, whatever starts it, needs the hand-over and ends there.
static const double SameChoice = 1.0, FuseSeconds = 8.0, FuseRest = 5.0;
enum { FuseChoices = 5 };
enum Choice { Held, Same, New };
static double reported[FuseChoices], held_until = 0;
static int reports = 0;

static Choice Chosen(double now)
{
    if (now < held_until)
    {
        held_until = now + FuseRest;
        return Held;
    }
    if (reports && now - reported[(reports - 1) % FuseChoices] < SameChoice) return Same;
    if (reports >= FuseChoices && now - reported[reports % FuseChoices] < FuseSeconds)
    {
        held_until = now + FuseRest;
        reports = 0;
        Say("storm");
        return Held;
    }
    reported[reports++ % FuseChoices] = now;
    return New;
}

@interface NEO98DockHelper : NSObject <NSApplicationDelegate>
@end

@implementation NEO98DockHelper
// The user chose this program. The windows are the host's, so the host is
// the application that has to be active: this one yields to it. Only the
// active application hands over: a helper that is not in front never brings
// the host in front of whatever the user works in now.
- (void)chosen:(const char *)message
{
    const Choice choice = Chosen(CFAbsoluteTimeGetCurrent());
    if (choice == Held) return;
    if (choice == New) Say(message);
    if (silent || !NSApp.active) return;
    NSRunningApplication *host = [NSRunningApplication runningApplicationWithProcessIdentifier:getppid()];
    if (!host) return;
    if (@available(macOS 14.0, *)) [NSApp yieldActivationToApplication:host];
    // The host brings the windows of this program forward itself.
    [host activateWithOptions:0];
}
- (void)applicationDidBecomeActive:(NSNotification *)note
{
    [self chosen:"activate"];
}
- (BOOL)applicationShouldHandleReopen:(NSApplication *)sender hasVisibleWindows:(BOOL)flag
{
    [self chosen:"reopen"];
    return NO;
}
// Quit in the Dock menu or Cmd+Q closes the program in the guest (WM_CLOSE).
// This process ends when the host says the program is gone.
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
    Say("quit");
    return NSTerminateCancel;
}
@end

int main(int argc, char **argv)
{
    @autoreleasepool
    {
        for (int i = 1; i < argc; ++i)
        {
            if (!strcmp(argv[i], "--channel") && i + 1 < argc) channel = atoi(argv[++i]);
            else if (!strcmp(argv[i], "--silent")) silent = YES;
        }
        NSApplication *app = [NSApplication sharedApplication];
        static NEO98DockHelper *delegate;
        delegate = [NEO98DockHelper new];
        app.delegate = delegate;
        [app setActivationPolicy:silent ? NSApplicationActivationPolicyProhibited : NSApplicationActivationPolicyRegular];
        // The menu bar while this application is the active one, which is
        // only until the host has taken over: its name and Quit.
        NSString *name = NSBundle.mainBundle.infoDictionary[@"CFBundleName"] ?: NSProcessInfo.processInfo.processName;
        NSMenu *bar = [[NSMenu alloc] initWithTitle:name];
        NSMenuItem *holder = [[NSMenuItem alloc] initWithTitle:name action:nil keyEquivalent:@""];
        NSMenu *menu = [[NSMenu alloc] initWithTitle:name];
        [menu addItemWithTitle:[@"Quit " stringByAppendingString:name] action:@selector(terminate:) keyEquivalent:@"q"];
        holder.submenu = menu;
        [bar addItem:holder];
        app.mainMenu = bar;
        // The host's lines: "bye", or the end of the channel when it is gone.
        dispatch_source_t source = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, (uintptr_t)channel, 0,
                                                          dispatch_get_main_queue());
        dispatch_source_set_event_handler(source, ^{
            char buffer[256];
            const ssize_t n = read(channel, buffer, sizeof buffer - 1);
            if (n <= 0) exit(0);
            buffer[n] = 0;
            if (strstr(buffer, "bye")) exit(0);
        });
        dispatch_resume(source);
        Say("hello");
        if (const char *script = getenv("NEO98_DOCK_HELPER_SCRIPT"))
        {
            NSString *text = [NSString stringWithUTF8String:script];
            for (NSString *step in [text componentsSeparatedByString:@";"])
            {
                NSArray<NSString *> *parts = [step componentsSeparatedByString:@":"];
                if (parts.count != 2) continue;
                // "message@Name": only the helper of that program sends it.
                NSArray<NSString *> *aimed = [parts[1] componentsSeparatedByString:@"@"];
                if (aimed.count == 2 && ![aimed[1] isEqualToString:name]) continue;
                NSString *message = aimed[0];
                // "activate" and "reopen" take the way of the real ones.
                const BOOL choice = [message isEqualToString:@"activate"] || [message isEqualToString:@"reopen"];
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(parts[0].doubleValue * NSEC_PER_SEC)),
                               dispatch_get_main_queue(), ^{
                                   if (choice) [delegate chosen:message.UTF8String];
                                   else Say(message.UTF8String);
                               });
            }
        }
        [app run];
    }
    return 0;
}
