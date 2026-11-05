// SPDX-License-Identifier: GPL-2.0-or-later
// The macOS menu bar of the host window; see tools/host_menu.h and
//
#include "host_menu.h"
#import <AppKit/AppKit.h>
#import <objc/runtime.h>
#include <Carbon/Carbon.h>
#include <SDL.h>
#include <deque>
#include <string>
#include <unistd.h>

namespace
{
std::deque<HostMenu::Request> queue;
bool installed = false;
// NSApp was created here, not by SDL: the event loop is ours to drain.
bool own_application = false;
// A test answers the open panel with this path instead of a person.
std::string panel_answer;
bool panel_answered = false;
NSMenuItem *com_items[4], *cdrom_info, *drive_info, *eject_item, *scale_items[2], *fullscreen_item, *reboot_item, *mouse_item, *online_item, *pause_item, *pause_unfocused_item, *state_line, *state_save_item, *state_load_item, *state_list_item, *state_info,
    *network_info, *listen_item, *wifi_item, *microphone_item, *seamless_item, *seamless_taskbar_item, *seamless_windows_item,
    *seamless_icons_item, *seamless_dock_item, *wheel_focus_item;
NSMenu *windows_menu;
std::vector<std::pair<unsigned, std::string>> listed_windows;
bool online_state = true, listen_lan = false, wifi_state = false, microphone_state = true;

void Push(HostMenu::Action action, int value = 0, const std::string &path = std::string())
{
    HostMenu::Request request;
    request.action = action;
    request.value = value;
    request.path = path;
    queue.push_back(request);
}

// The open panel, or the answer a test gave in its place. Empty when the
// person cancels.
std::string dos_program;
std::string Choose(NSString *title, bool files, bool directories = true)
{
    if (panel_answered)
    {
        panel_answered = false;
        return panel_answer;
    }
    if (own_application)
        return std::string(); // no window server session to show a panel in
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.title = title;
    panel.message = title;
    panel.prompt = @"Choose";
    panel.canChooseDirectories = directories ? YES : NO;
    panel.canChooseFiles = files ? YES : NO;
    panel.allowsMultipleSelection = NO;
    panel.treatsFilePackagesAsDirectories = NO;
    std::string chosen;
    if ([panel runModal] == NSModalResponseOK && panel.URL.fileSystemRepresentation)
        chosen = panel.URL.fileSystemRepresentation;
    // The panel ate the key-up and mouse-up events of the click that closed
    // it; the guest must not keep a button it saw going down.
    SDL_ResetKeyboard();
    return chosen;
}
// Machine > Serial ports > COMn: where the port leads. True with the
// answer in endpoint, empty for a port without a cable to the Mac.
bool ChooseEndpoint(int port, const std::string &current, std::string &endpoint)
{
    if (panel_answered)
    {
        panel_answered = false;
        endpoint = panel_answer;
        return true;
    }
    if (own_application)
        return false;
    NSAlert *alert = [NSAlert new];
    alert.messageText = [NSString stringWithFormat:@"Serial port COM%d", port];
    alert.informativeText = @"Connect to a TCP endpoint (tcp:HOST:PORT) or a "
                            @"Mac serial device (/dev/cu.NAME). The connection opens when a "
                            @"program opens the port.";
    NSComboBox *box = [[NSComboBox alloc] initWithFrame:NSMakeRect(0, 0, 320, 26)];
    box.placeholderString = @"tcp:127.0.0.1:7898";
    box.stringValue = [NSString stringWithUTF8String:current.c_str()] ?: @"";
    [box addItemWithObjectValue:@"tcp:127.0.0.1:7898"];
    for (NSString *device in [NSFileManager.defaultManager contentsOfDirectoryAtPath:@"/dev" error:nil])
        if ([device hasPrefix:@"cu."])
            [box addItemWithObjectValue:[@"/dev/" stringByAppendingString:device]];
    alert.accessoryView = box;
    [alert addButtonWithTitle:@"Apply"];
    [alert addButtonWithTitle:@"Cancel"];
    [alert addButtonWithTitle:@"Disconnected"];
    alert.window.initialFirstResponder = box;
    const NSModalResponse answer = [alert runModal];
    SDL_ResetKeyboard();
    if (answer == NSAlertSecondButtonReturn)
        return false;
    endpoint = answer == NSAlertThirdButtonReturn
                   ? std::string()
                   : std::string([box.stringValue stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet].UTF8String ?: "");
    return true;
}
std::string com_endpoints[4];
} // namespace

@interface NEO98MenuTarget : NSObject
- (void)choose:(NSMenuItem *)item;
@end

@implementation NEO98MenuTarget
- (void)choose:(NSMenuItem *)item
{
    NSString *name = item.identifier;
    using HostMenu::Action;
    if ([name isEqualToString:@"reboot"])
        Push(Action::Reboot);
    else if ([name isEqualToString:@"report"])
        Push(Action::Report);
    else if ([name isEqualToString:@"cdrom-insert"])
    {
        const auto path = Choose(@"Mount an ISO image or folder as a CD-ROM", true);
        if (!path.empty())
            Push(Action::InsertCdrom, 0, path);
    }
    else if ([name isEqualToString:@"cdrom-eject"])
        Push(Action::EjectCdrom);
    else if ([name isEqualToString:@"drive-d"])
    {
        const auto path = Choose(@"Choose a folder for drive D:", false);
        if (!path.empty())
            Push(Action::ChooseDriveD, 0, path);
    }
    else if (name.length == 5 && [name hasPrefix:@"com-"])
    {
        const int port = [name characterAtIndex:4] - '0';
        std::string endpoint;
        if (port >= 1 && port <= 4 && ChooseEndpoint(port, com_endpoints[port - 1], endpoint))
            Push(Action::ComPort, port, endpoint);
    }
    else if ([name isEqualToString:@"scale-1"])
        Push(Action::Scale, 1);
    else if ([name isEqualToString:@"scale-2"])
        Push(Action::Scale, 2);
    else if ([name isEqualToString:@"fullscreen"])
        Push(Action::Fullscreen);
    else if ([name isEqualToString:@"seamless"])
        Push(Action::Seamless);
    else if ([name isEqualToString:@"seamless-taskbar"])
        Push(Action::SeamlessTaskbar);
    else if ([name isEqualToString:@"seamless-windows"])
        Push(Action::SeamlessWindows);
    else if ([name isEqualToString:@"seamless-dock"])
        Push(Action::SeamlessDock);
    else if ([name isEqualToString:@"seamless-icons"])
        Push(Action::SeamlessIcons);
    else if ([name isEqualToString:@"wheel-focus"])
        Push(Action::WheelFocus);
    else if ([name isEqualToString:@"start-menu"])
        Push(Action::StartMenu);
    else if ([name isEqualToString:@"guest-window"])
        Push(Action::GuestWindow, (int)item.tag);
    else if ([name isEqualToString:@"network-listen"])
        Push(Action::NetworkListen, listen_lan ? 0 : 1);
    else if ([name isEqualToString:@"network-online"])
        Push(Action::Network, online_state ? 0 : 1);
    else if ([name isEqualToString:@"network-wifi"])
        Push(Action::Wifi, wifi_state ? 0 : 1);
    else if ([name isEqualToString:@"microphone"])
        Push(Action::Microphone, microphone_state ? 0 : 1);
    else if ([name isEqualToString:@"minimize"])
        Push(Action::Minimize);
    else if ([name isEqualToString:@"mouse-release"])
        Push(Action::MouseRelease);
    else if ([name isEqualToString:@"ctrl-alt-delete"])
        Push(Action::CtrlAltDelete);
    else if ([name isEqualToString:@"pause"])
        Push(Action::Pause);
    else if ([name isEqualToString:@"pause-unfocused"])
        Push(Action::PauseUnfocused);
    // The states of a game machine; a test names the state in place of a click.
    else if ([name isEqualToString:@"state-save"])
        Push(Action::StateSave, 0, panel_answered ? panel_answer : std::string());
    else if ([name isEqualToString:@"state-load"])
        Push(Action::StateLoad, 0, panel_answered ? panel_answer : std::string());
    else if ([name isEqualToString:@"state-list"])
        Push(Action::StateList);
    else if ([name isEqualToString:@"quit"])
        Push(Action::Quit);
    else if ([name isEqualToString:@"edit-cut"])
        Push(Action::Edit, 'X');
    else if ([name isEqualToString:@"edit-copy"])
        Push(Action::Edit, 'C');
    else if ([name isEqualToString:@"edit-paste"])
        Push(Action::Edit, 'V');
    else if ([name isEqualToString:@"open-program"])
    {
        const auto path = Choose(@"Open a program or document in Windows 98", true, false);
        if (!path.empty())
            Push(Action::Open, 0, path);
    }
    else if ([name isEqualToString:@"dos-settings"] || [name isEqualToString:@"dos-settings-file"])
    {
        const auto path = !panel_answered && [name isEqualToString:@"dos-settings"] && !dos_program.empty()
                        ? dos_program : Choose(@"Choose a DOS program or PIF", true, false);
        if (!path.empty()) Push(Action::DosSettings, 0, path);
    }
    else if ([name isEqualToString:@"settings"])
        // A test names the new machine folder in place of the window.
        Push(Action::Preferences, 0, panel_answered ? panel_answer : std::string());
    else if ([name isEqualToString:@"about"])
    {
        [NSApp activateIgnoringOtherApps:YES];
        [NSApp orderFrontStandardAboutPanelWithOptions:@{
            NSAboutPanelOptionApplicationName : @"Neo98",
            NSAboutPanelOptionCredits : [[NSAttributedString alloc]
                initWithString:@"Windows 98 applications on your Apple silicon Mac."],
        }];
    }
}
@end

namespace
{
NEO98MenuTarget *target;

NSMenuItem *Item(NSMenu *menu, NSString *title, NSString *name, NSString *key,
                 NSEventModifierFlags modifiers = NSEventModifierFlagCommand)
{
    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title action:@selector(choose:) keyEquivalent:key ?: @""];
    item.identifier = name;
    item.target = target;
    if (key.length)
        item.keyEquivalentModifierMask = modifiers;
    [menu addItem:item];
    return item;
}
NSMenuItem *Info(NSMenu *menu)
{
    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
    item.enabled = NO;
    [menu addItem:item];
    return item;
}
NSMenu *Submenu(NSMenu *bar, NSString *title)
{
    NSMenuItem *holder = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
    NSMenu *menu = [[NSMenu alloc] initWithTitle:title];
    menu.autoenablesItems = NO;
    holder.submenu = menu;
    [bar addItem:holder];
    return menu;
}
NSMenuItem *Find(NSMenu *menu, NSString *name, NSMenu **owner)
{
    for (NSMenuItem *item in menu.itemArray)
    {
        if ([item.identifier isEqualToString:name])
        {
            *owner = menu;
            return item;
        }
        if (item.submenu)
            if (NSMenuItem *found = Find(item.submenu, name, owner))
                return found;
    }
    return nil;
}
std::string Utf8(NSString *text) { return text ? std::string(text.UTF8String) : std::string(); }
std::string Shortcut(NSMenuItem *item)
{
    if (!item.keyEquivalent.length)
        return std::string();
    std::string text;
    const auto mask = item.keyEquivalentModifierMask;
    if (mask & NSEventModifierFlagControl)
        text += "ctrl+";
    if (mask & NSEventModifierFlagOption)
        text += "alt+";
    if (mask & NSEventModifierFlagShift)
        text += "shift+";
    if (mask & NSEventModifierFlagCommand)
        text += "cmd+";
    return text + Utf8(item.keyEquivalent.lowercaseString);
}
void Describe(NSMenu *menu, const std::string &path, std::string &out)
{
    for (NSMenuItem *item in menu.itemArray)
    {
        // An item that is hidden is not in the menu (the states of a game
        // machine in the normal machine).
        if (item.separatorItem || item.hidden)
            continue;
        const std::string title = path.empty() ? Utf8(item.title) : path + " > " + Utf8(item.title);
        if (item.submenu)
        {
            Describe(item.submenu, title, out);
            continue;
        }
        out += title;
        if (item.identifier.length)
            out += " [" + Utf8(item.identifier) + "]";
        if (const auto keys = Shortcut(item); !keys.empty())
            out += " " + keys;
        if (!item.enabled)
            out += " (disabled)";
        if (item.state == NSControlStateValueOn)
            out += " (enabled)";
        out += "\n";
    }
}
std::string Leaf(std::string path)
{
    while (path.size() > 1 && path.back() == '/')
        path.pop_back();
    const auto slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}
} // namespace

// The files of the launch, for a start that only hands them on.
@interface NEO98LaunchDocuments : NSObject <NSApplicationDelegate>
@property(strong) NSMutableArray<NSString *> *paths;
@property(strong) NSMutableArray<NSString *> *games;
@end
@implementation NEO98LaunchDocuments
- (instancetype)init
{
    if ((self = [super init]))
    {
        _paths = [NSMutableArray array];
        _games = [NSMutableArray array];
    }
    return self;
}
// The service "Open as game machine" (NEO98GameService in
// tools/host_game_menu.mm), for a start that only hands on.
- (void)openAsGameMachine:(NSPasteboard *)pasteboard userData:(NSString *)data error:(NSString **)error
{
    for (NSURL *url in [pasteboard readObjectsForClasses:@[ NSURL.class ]
                                                 options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}])
        if (url.path)
            [_games addObject:url.path];
}
- (void)application:(NSApplication *)application openURLs:(NSArray<NSURL *> *)urls
{
    for (NSURL *url in urls)
        if (url.isFileURL && url.path)
            [_paths addObject:url.path];
}
@end

// What the launch was for (HostMenu::LaunchWatch). AppKit posts
// NSApplicationDidFinishLaunchingNotification while it handles the first
// Apple event of the launch, so that event is the current one here.
@interface NEO98LaunchWatch : NSObject
@property BOOL finished;
@property BOOL plain;
@property BOOL service;
@property(strong) NSString *event;
- (void)launched:(NSNotification *)notification;
@end
@implementation NEO98LaunchWatch
- (void)launched:(NSNotification *)notification
{
    _finished = YES;
    NSNumber *plain = notification.userInfo[NSApplicationLaunchIsDefaultLaunchKey];
    _plain = plain ? plain.boolValue : YES;
    const auto code = [](unsigned value) {
        char text[5] = {char(value >> 24), char(value >> 16), char(value >> 8), char(value), 0};
        for (char &c : text)
            if (c && (c < 0x20 || c > 0x7e))
                c = '?';
        return [NSString stringWithUTF8String:value ? text : "-"];
    };
    NSAppleEventDescriptor *event = NSAppleEventManager.sharedAppleEventManager.currentAppleEvent;
    NSAppleEventDescriptor *why = [event paramDescriptorForKeyword:keyAEPropData];
    const unsigned reason = why ? (why.enumCodeValue ?: why.typeCodeValue) : 0;
    // keyAELaunchedAsServiceItem: "application was launched as a service
    // item" (AERegistry.h); the request of the service follows.
    _service = event && event.eventID == kAEOpenApplication && reason == keyAELaunchedAsServiceItem;
    if (_service)
        _plain = NO;
    _event = event ? [NSString stringWithFormat:@"%@/%@ reason=%@", code(event.eventClass), code(event.eventID), code(reason)]
                   : @"none";
}
@end

namespace HostMenu
{
namespace
{
NSMenu *DockMenuOfDelegate(id, SEL, NSApplication *);
NEO98LaunchWatch *launch_watch;
} // namespace

void LaunchWatch()
{
    @autoreleasepool
    {
        if (launch_watch)
            return;
        launch_watch = [NEO98LaunchWatch new];
        launch_watch.plain = YES;
        [NSNotificationCenter.defaultCenter addObserver:launch_watch
                                               selector:@selector(launched:)
                                                   name:NSApplicationDidFinishLaunchingNotification
                                                 object:nil];
    }
}

Launch LaunchWait(double launch_limit, double service_limit, bool (*arrived)())
{
    Launch launch;
    if (!launch_watch)
        return launch;
    @autoreleasepool
    {
        if (!NSApp)
        {
            // As Install does under another SDL video driver than Cocoa.
            [NSApplication sharedApplication];
            [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
            [NSApp finishLaunching];
            own_application = true;
        }
        NSDate *start = [NSDate date];
        // SDL's own loop where the application is SDL's: what it makes of an
        // event (a file of the launch becomes SDL_DROPFILE) stays in its
        // queue for whoever reads it, here or in the main loop.
        const auto serve = [&]() {
            if (own_application)
            {
                NSEvent *event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                    untilDate:[NSDate dateWithTimeIntervalSinceNow:0.004]
                                                       inMode:NSDefaultRunLoopMode
                                                      dequeue:YES];
                if (event)
                    [NSApp sendEvent:event];
            }
            else
            {
                SDL_PumpEvents();
                if (!launch_watch.finished || launch_watch.service)
                    usleep(4000);
            }
        };
        while (!launch_watch.finished && -start.timeIntervalSinceNow < launch_limit)
            serve();
        NSDate *came = [NSDate date];
        while (launch_watch.service && !(arrived && arrived()) && -came.timeIntervalSinceNow < service_limit)
            serve();
        launch.finished = launch_watch.finished;
        launch.plain = launch_watch.plain;
        launch.service = launch_watch.service;
        launch.event = launch_watch.event.UTF8String ?: "none";
        launch.seconds = -start.timeIntervalSinceNow;
        [NSNotificationCenter.defaultCenter removeObserver:launch_watch];
        launch_watch = nil;
    }
    return launch;
}

bool Install()
{
    @autoreleasepool
    {
        if (installed)
            return true;
        if (!NSApp)
        {
            // Another SDL video driver than Cocoa: the menu still exists, for
            // the test switches, but the process stays out of the Dock.
            [NSApplication sharedApplication];
            [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
            [NSApp finishLaunching];
            own_application = true;
        }
        if (!NSApp)
            return false;
        target = [NEO98MenuTarget new];
        NSMenu *bar = [[NSMenu alloc] initWithTitle:@"Neo98"];
        NSMenu *app = Submenu(bar, @"Neo98");
        Item(app, @"About Neo98", @"about", nil);
        [app addItem:[NSMenuItem separatorItem]];
        Item(app, @"Settings …", @"settings", @",");
        [app addItem:[NSMenuItem separatorItem]];
        Item(app, @"Quit Neo98", @"quit", @"q");

        // What a double click in the Finder does, from an open panel.
        NSMenu *file = Submenu(bar, @"File");
        Item(file, @"Open program …", @"open-program", @"o");
        Item(file, @"DOS program settings …", @"dos-settings", nil);
        Item(file, @"Configure another DOS file …", @"dos-settings-file", nil);

        // The keys of the Mac for the clipboard. The program in the guest
        // gets Ctrl+X, Ctrl+C or Ctrl+V and does what it does with them; the
        // clipboard itself is shared (docs/BUILDING.md, "Clipboard").
        NSMenu *edit = Submenu(bar, @"Edit");
        Item(edit, @"Cut", @"edit-cut", @"x");
        Item(edit, @"Copy", @"edit-copy", @"c");
        Item(edit, @"Paste", @"edit-paste", @"v");

        NSMenu *machine = Submenu(bar, @"Machine");
        // Cmd+R alone stays with the guest: it is Win+R, the Run dialog.
        reboot_item = Item(machine, @"Restart", @"reboot", @"r",
                           NSEventModifierFlagCommand | NSEventModifierFlagShift);
        Item(machine, @"Write crash report", @"report", @"b",
             NSEventModifierFlagCommand | NSEventModifierFlagShift);
        // The whole machine stands and goes on. With Shift,
        // like the two above: Cmd+P alone would be Win+P for the guest.
        pause_item = Item(machine, @"Pause", @"pause", @"p", NSEventModifierFlagCommand | NSEventModifierFlagShift);
        pause_unfocused_item = Item(machine, @"Pause when Neo98 loses focus", @"pause-unfocused", nil);
        // The saved states of a game machine; hidden in
        // the normal machine. With Shift like the items above: Cmd and a
        // letter alone is the Windows key and that letter for the guest.
        state_line = [NSMenuItem separatorItem];
        [machine addItem:state_line];
        state_save_item = Item(machine, @"Save state", @"state-save", @"s", NSEventModifierFlagCommand | NSEventModifierFlagShift);
        state_load_item = Item(machine, @"Load latest state", @"state-load", @"l", NSEventModifierFlagCommand | NSEventModifierFlagShift);
        state_list_item = Item(machine, @"States …", @"state-list", @"z", NSEventModifierFlagCommand | NSEventModifierFlagShift);
        state_info = Info(machine);
        for (NSMenuItem *item in @[state_line, state_save_item, state_load_item, state_list_item, state_info])
            item.hidden = YES;
        [machine addItem:[NSMenuItem separatorItem]];
        // The way out of a program that holds the mouse; the keys are
        // Ctrl+Option alone, which a menu cannot have as its equivalent.
        mouse_item = Item(machine, @"Release mouse (Ctrl+Option)", @"mouse-release", nil);
        Item(machine, @"Send Ctrl+Alt+Delete", @"ctrl-alt-delete", @"\x7f",
             NSEventModifierFlagControl | NSEventModifierFlagOption);
        [machine addItem:[NSMenuItem separatorItem]];
        Item(machine, @"Mount ISO or folder as CD-ROM …", @"cdrom-insert", nil);
        eject_item = Item(machine, @"Eject CD-ROM", @"cdrom-eject", nil);
        cdrom_info = Info(machine);
        [machine addItem:[NSMenuItem separatorItem]];
        Item(machine, @"Choose drive D: …", @"drive-d", nil);
        drive_info = Info(machine);
        [machine addItem:[NSMenuItem separatorItem]];
        // COM1 to COM4 of the guest and where each leads.
        NSMenu *serial = Submenu(machine, @"Serial ports");
        for (int port = 0; port < 4; ++port)
            com_items[port] = Item(serial, @"", [NSString stringWithFormat:@"com-%d", port + 1], nil);
        // The microphone of the Mac as the recording device of Windows.
        microphone_item = Item(machine, @"Microphone in Windows", @"microphone", nil);
        microphone_item.toolTip = @"Windows programs record through your Mac's microphone. macOS asks for permission on first use.";

        // The network cable of the guest's card: out, the guest keeps its
        // adapter and address and the network stops answering.
        NSMenu *network = Submenu(bar, @"Network");
        online_item = Item(network, @"Internet connected", @"network-online", nil);
        listen_item = Item(network, @"Allow LAN connections", @"network-listen", nil);
        listen_item.toolTip = @"Applies to newly opened game ports. Restart running game servers.";
        wifi_item = Item(network, @"Wi-Fi in Windows", @"network-wifi", nil);
        wifi_item.toolTip = @"Windows shows your Mac's Wi-Fi in the system tray and lets you select networks.";
        network_info = Info(network);

        NSMenu *view = Submenu(bar, @"View");
        scale_items[0] = Item(view, @"Scale 1x", @"scale-1", @"1");
        scale_items[1] = Item(view, @"Scale 2x", @"scale-2", @"2");
        [view addItem:[NSMenuItem separatorItem]];
        fullscreen_item = Item(view, @"Fullscreen", @"fullscreen", @"f",
                               NSEventModifierFlagCommand | NSEventModifierFlagControl);
        // The windows of the guest beside the windows of the Mac.
        seamless_item = Item(view, @"Seamless", @"seamless", @"s",
                             NSEventModifierFlagCommand | NSEventModifierFlagControl);
        seamless_taskbar_item = Item(view, @"Taskbar in seamless mode", @"seamless-taskbar", nil);
        seamless_windows_item = Item(view, @"One Mac window per guest window", @"seamless-windows", nil);
        seamless_icons_item = Item(view, @"Show desktop icons", @"seamless-icons", nil);
        // Every program of the guest with an icon of its own in the Dock.
        seamless_dock_item = Item(view, @"One Dock icon per program", @"seamless-dock", nil);
        // The wheel and the trackpad scroll the window under the pointer;
        // with this item the window with the focus, as Win98 itself does.
        [view addItem:[NSMenuItem separatorItem]];
        wheel_focus_item = Item(view, @"Mouse wheel scrolls the focused window (Windows 98 behavior)", @"wheel-focus", nil);
        // Mission Control and this menu list the guest's windows by title.
        NSMenu *windows = Submenu(bar, @"Window");
        NSApp.windowsMenu = windows;
        windows_menu = windows;
        // The taskbar of the guest is not shown in the seamless view by
        // default; its Start menu opens from here.
        // The guest's foreground window into the Dock, as its own minimize
        // button does it.
        Item(windows, @"Minimize", @"minimize", @"m");
        Item(windows, @"Start menu", @"start-menu", nil);
        [NSApp setMainMenu:bar];
        installed = true;
        // The delegate is SDL's; it gets the Dock menu added.
        if (id delegate = NSApp.delegate; delegate && ![delegate respondsToSelector:@selector(applicationDockMenu:)])
            class_addMethod([delegate class], @selector(applicationDockMenu:), (IMP)DockMenuOfDelegate, "@@:@");
        Update(State());
        return true;
    }
}

bool Installed() { return installed; }

namespace
{
// The menu of the Dock icon: the Start menu of the guest and the guest
// windows without a Mac window of their own. AppKit adds the Mac windows of
// the guest windows itself.
NSMenu *DockMenu()
{
    NSMenu *menu = [[NSMenu alloc] initWithTitle:@"Dock"];
    menu.autoenablesItems = NO;
    Item(menu, @"Start menu", @"start-menu", nil);
    for (const auto &entry : listed_windows)
    {
        NSMenuItem *item = Item(menu, [NSString stringWithUTF8String:entry.second.c_str()] ?: @"?", @"guest-window", nil);
        item.tag = (NSInteger)entry.first;
    }
    return menu;
}
NSMenu *DockMenuOfDelegate(id, SEL, NSApplication *) { return DockMenu(); }
} // namespace

void Update(const State &state)
{
    dos_program = state.dos_program;
    if (!installed)
        return;
    @autoreleasepool
    {
        const bool windowed = !state.fullscreen && !state.seamless;
        scale_items[0].state = state.scale == 1 && windowed ? NSControlStateValueOn : NSControlStateValueOff;
        scale_items[1].state = state.scale == 2 && windowed ? NSControlStateValueOn : NSControlStateValueOff;
        fullscreen_item.state = state.fullscreen ? NSControlStateValueOn : NSControlStateValueOff;
        seamless_item.state = state.seamless ? NSControlStateValueOn : NSControlStateValueOff;
        seamless_taskbar_item.state = state.seamless_taskbar ? NSControlStateValueOn : NSControlStateValueOff;
        seamless_windows_item.state = state.seamless_windows ? NSControlStateValueOn : NSControlStateValueOff;
        seamless_icons_item.state = state.seamless_icons ? NSControlStateValueOn : NSControlStateValueOff;
        wheel_focus_item.state = state.wheel_focus ? NSControlStateValueOn : NSControlStateValueOff;
        seamless_dock_item.state = state.seamless_dock ? NSControlStateValueOn : NSControlStateValueOff;
        // The guest windows without a Mac window of their own, by title.
        if (state.guest_windows != listed_windows)
        {
            listed_windows = state.guest_windows;
            for (NSMenuItem *old in windows_menu.itemArray.copy)
                if ([old.identifier isEqualToString:@"guest-window"] || [old.identifier isEqualToString:@"guest-windows"])
                    [windows_menu removeItem:old];
            if (!listed_windows.empty())
            {
                NSMenuItem *line = [NSMenuItem separatorItem];
                line.identifier = @"guest-windows";
                [windows_menu addItem:line];
            }
            for (const auto &entry : listed_windows)
            {
                NSMenuItem *item = Item(windows_menu, [NSString stringWithUTF8String:entry.second.c_str()] ?: @"?",
                                        @"guest-window", nil);
                item.tag = (NSInteger)entry.first;
            }
        }
        eject_item.enabled = !state.cdrom.empty();
        const std::string cd = state.cdrom.empty()
                                   ? std::string("No CD-ROM mounted")
                                   : std::string("CD-ROM ") + state.cdrom_letter + ": " + Leaf(state.cdrom);
        cdrom_info.title = [NSString stringWithUTF8String:cd.c_str()];
        const std::string d = state.drive_d.empty() ? std::string("D: not mounted") : "D: " + state.drive_d;
        drive_info.title = [NSString stringWithUTF8String:d.c_str()];
        for (int port = 0; port < 4; ++port)
        {
            com_endpoints[port] = state.com[port];
            const std::string line = "COM" + std::to_string(port + 1) + ": " +
                                     (state.com[port].empty() ? std::string("disconnected") : state.com[port]) + " …";
            com_items[port].title = [NSString stringWithUTF8String:line.c_str()] ?: @"COM";
        }
        reboot_item.title = state.hang ? @"Restart (guest not responding)" : @"Restart";
        mouse_item.enabled = state.mouse == 1;
        mouse_item.title = state.mouse == 2 ? @"Mouse released (click the window to capture it)"
                                            : @"Release mouse (Ctrl+Option)";
        pause_item.state = state.paused ? NSControlStateValueOn : NSControlStateValueOff;
        pause_unfocused_item.state = state.pause_unfocused ? NSControlStateValueOn : NSControlStateValueOff;
        for (NSMenuItem *item in @[state_line, state_save_item, state_load_item, state_list_item])
            item.hidden = !state.states;
        // Grey with the reason below them, instead of an item that does nothing.
        state_save_item.enabled = state.states_blocked.empty();
        state_load_item.enabled = state.states_last;
        state_info.hidden = !state.states || state.states_blocked.empty();
        state_info.title = [NSString stringWithUTF8String:state.states_blocked.c_str()] ?: @"";
        online_state = state.online;
        listen_lan = state.net_listen.compare(0, 4, "127.") != 0;
        listen_item.state = listen_lan ? NSControlStateValueOn : NSControlStateValueOff;
        online_item.state = state.online ? NSControlStateValueOn : NSControlStateValueOff;
        wifi_state = state.wifi;
        wifi_item.state = state.wifi ? NSControlStateValueOn : NSControlStateValueOff;
        microphone_state = state.microphone;
        microphone_item.state = state.microphone ? NSControlStateValueOn : NSControlStateValueOff;
        const std::string card = "Network adapter: " + (state.online ? (state.address.empty() ? std::string("disconnected")
                                                                                         : state.address)
                                                                   : std::string("disconnected"));
        network_info.title = [NSString stringWithUTF8String:card.c_str()];
    }
}

void FullScreenBars(bool fullscreen)
{
    if (!installed)
        return;
    const NSApplicationPresentationOptions hidden =
                                               NSApplicationPresentationHideMenuBar | NSApplicationPresentationHideDock,
                                           reveal = NSApplicationPresentationAutoHideMenuBar |
                                                    NSApplicationPresentationAutoHideDock;
    const NSApplicationPresentationOptions now = NSApp.presentationOptions;
    if (fullscreen ? (now & hidden) != 0 || (now & reveal) != reveal : (now & reveal) != 0)
        NSApp.presentationOptions = fullscreen ? reveal : NSApplicationPresentationDefault;
}

void Pump()
{
    if (!installed || !own_application)
        return;
    @autoreleasepool
    {
        while (NSEvent *event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                   untilDate:[NSDate distantPast]
                                                      inMode:NSDefaultRunLoopMode
                                                     dequeue:YES])
            [NSApp sendEvent:event];
    }
}

bool Activate()
{
    if (!NSApp || own_application)
        return false;
    @autoreleasepool
    {
        [NSApp unhide:nil];
        // The cooperative -activate of macOS 14 does not take the front from
        // the terminal that started the host; this one still does.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [NSApp activateIgnoringOtherApps:YES];
#pragma clang diagnostic pop
        NSWindow *window = NSApp.mainWindow ?: NSApp.windows.firstObject;
        for (NSWindow *candidate in NSApp.windows)
            if (candidate.isVisible && candidate.canBecomeKeyWindow) { window = candidate; break; }
        if (window.canBecomeKeyWindow)
            [window makeKeyAndOrderFront:nil];
    }
    return true;
}

void ActivateProcess(long process)
{
    @autoreleasepool
    {
        NSRunningApplication *other = [NSRunningApplication runningApplicationWithProcessIdentifier:pid_t(process)];
        if (!other)
            return;
        // Since macOS 14 the application in front hands the activation on.
        if (@available(macOS 14.0, *))
            if (NSApp)
                [NSApp yieldActivationToApplication:other];
        [other activateWithOptions:NSApplicationActivateAllWindows];
    }
}

std::vector<std::string> LaunchDocuments(std::vector<std::string> *as_game)
{
    std::vector<std::string> documents;
    @autoreleasepool
    {
        [NSApplication sharedApplication];
        NEO98LaunchDocuments *delegate = [NEO98LaunchDocuments new];
        NSApp.delegate = delegate;
        if (as_game)
            NSApp.servicesProvider = delegate;
        [NSApp finishLaunching];
        // The event follows the launch at once; a start without a file waits
        // this long for nothing.
        NSDate *end = [NSDate dateWithTimeIntervalSinceNow:0.7];
        while ([end timeIntervalSinceNow] > 0)
        {
            NSEvent *event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                untilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]
                                                   inMode:NSDefaultRunLoopMode
                                                  dequeue:YES];
            if (event)
                [NSApp sendEvent:event];
            if ((delegate.paths.count || delegate.games.count) && [end timeIntervalSinceNow] > 0.15)
                end = [NSDate dateWithTimeIntervalSinceNow:0.15];
        }
        for (NSString *path in delegate.paths)
            documents.push_back(path.fileSystemRepresentation);
        for (NSString *path in delegate.games)
            if (as_game)
                as_game->push_back(path.fileSystemRepresentation);
        NSApp.servicesProvider = nil;
        NSApp.delegate = nil;
    }
    return documents;
}

bool Next(Request &request)
{
    if (queue.empty())
        return false;
    request = queue.front();
    queue.pop_front();
    return true;
}

void Post(const Request &request) { queue.push_back(request); }

void Notice(const char *title, const char *message)
{
    printf("NEO98 HOST notice %s: %s\n", title, message);
    fflush(stdout);
    if (!installed || own_application) return;
    @autoreleasepool
    {
        NSAlert *alert = [NSAlert new];
        alert.messageText = [NSString stringWithUTF8String:title];
        alert.informativeText = [NSString stringWithUTF8String:message];
        [alert addButtonWithTitle:@"OK"];
        [alert runModal];
        SDL_ResetKeyboard();
    }
}

int DosFailure(const char *path, const char *message)
{
    printf("NEO98 HOST DOS failure %s: %s\n", path, message);
    fflush(stdout);
    if (const char *action = getenv("NEO98_DOS_FAILURE_ACTION"))
        return !strcmp(action, "settings") ? 1 : !strcmp(action, "retry") ? 2 : 0;
    if (!installed || own_application) return 0;
    @autoreleasepool
    {
        NSAlert *alert = [NSAlert new];
        alert.messageText = @"Could not run the DOS program";
        alert.informativeText = [NSString stringWithUTF8String:(std::string(path) + "\n" + message).c_str()];
        [alert addButtonWithTitle:@"Close"];
        [alert addButtonWithTitle:@"Settings …"];
        [alert addButtonWithTitle:@"Restart"];
        const auto result = [alert runModal];
        SDL_ResetKeyboard();
        return result == NSAlertSecondButtonReturn ? 1 : result == NSAlertThirdButtonReturn ? 2 : 0;
    }
}

bool Perform(const char *name, const char *path)
{
    if (!installed)
        return false;
    @autoreleasepool
    {
        NSMenu *owner = nil;
        NSMenuItem *item = Find(NSApp.mainMenu, [NSString stringWithUTF8String:name], &owner);
        // guest-window=TITLE: the guest window of that title in "Window".
        if (path && !strcmp(name, "guest-window"))
        {
            item = nil;
            owner = windows_menu;
            for (NSMenuItem *entry in windows_menu.itemArray)
                if ([entry.identifier isEqualToString:@"guest-window"] &&
                    [entry.title isEqualToString:[NSString stringWithUTF8String:path]])
                    item = entry;
        }
        if (!item || !item.enabled)
            return false;
        panel_answered = path != nullptr;
        panel_answer = path ? path : "";
        [owner performActionForItemAtIndex:[owner indexOfItem:item]];
        panel_answered = false;
        return true;
    }
}

bool PostShortcut(const char *keys)
{
    if (!installed)
        return false;
    NSEventModifierFlags flags = 0;
    std::string text = keys;
    for (;;)
    {
        const auto plus = text.find('+');
        if (plus == std::string::npos)
            break;
        const std::string part = text.substr(0, plus);
        text = text.substr(plus + 1);
        if (part == "cmd")
            flags |= NSEventModifierFlagCommand;
        else if (part == "shift")
            flags |= NSEventModifierFlagShift;
        else if (part == "ctrl")
            flags |= NSEventModifierFlagControl;
        else if (part == "alt")
            flags |= NSEventModifierFlagOption;
        else
            return false;
    }
    static const struct { const char *name; unsigned short code; } codes[] = {
        {"r", kVK_ANSI_R}, {"b", kVK_ANSI_B}, {"q", kVK_ANSI_Q}, {"f", kVK_ANSI_F},
        {"1", kVK_ANSI_1}, {"2", kVK_ANSI_2}, {"o", kVK_ANSI_O}, {",", kVK_ANSI_Comma}, {"s", kVK_ANSI_S},
        {"x", kVK_ANSI_X}, {"c", kVK_ANSI_C}, {"v", kVK_ANSI_V}, {"p", kVK_ANSI_P}};
    for (const auto &code : codes)
        if (text == code.name)
            @autoreleasepool
            {
                NSString *character = [NSString stringWithUTF8String:code.name];
                NSEvent *event = [NSEvent keyEventWithType:NSEventTypeKeyDown
                                                  location:NSZeroPoint
                                             modifierFlags:flags
                                                 timestamp:[[NSProcessInfo processInfo] systemUptime]
                                              windowNumber:NSApp.keyWindow.windowNumber
                                                   context:nil
                                                characters:character
                                 charactersIgnoringModifiers:character
                                                 isARepeat:NO
                                                   keyCode:code.code];
                [NSApp postEvent:event atStart:NO];
                return true;
            }
    return false;
}

std::string Describe()
{
    std::string out;
    if (installed)
        @autoreleasepool
        {
            ::Describe(NSApp.mainMenu, std::string(), out);
            ::Describe(DockMenu(), "Dock", out);
        }
    return out;
}

namespace
{
bool Hdiutil(NSArray<NSString *> *arguments, NSData **output, std::string &error)
{
    NSTask *task = [NSTask new];
    task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/hdiutil"];
    task.arguments = arguments;
    NSPipe *out = [NSPipe pipe], *err = [NSPipe pipe];
    task.standardOutput = out;
    task.standardError = err;
    task.standardInput = [NSFileHandle fileHandleWithNullDevice];
    NSError *failure = nil;
    if (![task launchAndReturnError:&failure])
    {
        error = Utf8(failure.localizedDescription);
        return false;
    }
    NSData *data = [out.fileHandleForReading readDataToEndOfFile];
    NSData *message = [err.fileHandleForReading readDataToEndOfFile];
    [task waitUntilExit];
    if (output)
        *output = data;
    if (task.terminationStatus)
    {
        error = std::string((const char *)message.bytes, message.length);
        while (!error.empty() && (error.back() == '\n' || error.back() == '\r'))
            error.pop_back();
        if (error.empty())
            error = "hdiutil exit " + std::to_string(task.terminationStatus);
        return false;
    }
    return true;
}

bool ExistingMount(NSString *image, std::string &mount_point)
{
    NSData *data = nil;
    std::string error;
    if (!Hdiutil(@[ @"info", @"-plist" ], &data, error)) return false;
    NSDictionary *plist = [NSPropertyListSerialization propertyListWithData:data options:0 format:nil error:nil];
    NSString *wanted = image.stringByStandardizingPath.stringByResolvingSymlinksInPath;
    for (NSDictionary *entry in plist[@"images"])
    {
        NSString *path = entry[@"image-path"];
        if (![path isKindOfClass:NSString.class] ||
            ![path.stringByStandardizingPath.stringByResolvingSymlinksInPath isEqualToString:wanted]) continue;
        for (NSDictionary *entity in entry[@"system-entities"])
            if (NSString *mount = entity[@"mount-point"]; [mount isKindOfClass:NSString.class] && mount.length)
            {
                mount_point = mount.fileSystemRepresentation;
                return true;
            }
    }
    return false;
}
} // namespace

bool Attach(const char *image, std::string &mount_point, bool &owned, std::string &error)
{
    @autoreleasepool
    {
        owned = false;
        mount_point.clear();
        NSString *path = [NSString stringWithUTF8String:image];
        if (ExistingMount(path, mount_point)) return true;
        NSData *data = nil;
        if (!Hdiutil(@[ @"attach", @"-readonly", @"-nobrowse", @"-noverify", @"-noautofsck", @"-plist",
                        path ],
                     &data, error))
        {
            // Finder may have mounted it between info and attach.
            if (ExistingMount(path, mount_point)) { error.clear(); return true; }
            return false;
        }
        NSDictionary *plist = [NSPropertyListSerialization propertyListWithData:data options:0 format:nil error:nil];
        for (NSDictionary *entity in plist[@"system-entities"])
            if (NSString *mount = entity[@"mount-point"]; mount.length)
            {
                mount_point = mount.fileSystemRepresentation;
                owned = true;
                return true;
            }
        error = "hdiutil did not mount a filesystem";
        return false;
    }
}

bool Detach(const char *mount_point, std::string &error)
{
    @autoreleasepool
    {
        // The volume provider still holds a descriptor into the image, and
        // execv hands it to the restarted host; -force unmounts regardless.
        return Hdiutil(@[ @"detach", @"-force", [NSString stringWithUTF8String:mount_point] ], nil, error);
    }
}
} // namespace HostMenu
