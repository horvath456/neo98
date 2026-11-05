// SPDX-License-Identifier: GPL-2.0-or-later
// Machine > Game machines; see tools/host_game_menu.h and
//
#include "host_game_menu.h"
#include "host_game_machine.h"
#include "host_savestate.h"
#include "host_menu.h"
#import <AppKit/AppKit.h>
#include <SDL.h>
#include <deque>

namespace
{
std::deque<HostGameMenu::Request> queue;
NSMenu *games_menu;
std::string machine_folder, drive_folder, front_program;
// A test answers the panels with this text.
std::string test_answer;
bool test_answered = false;
// And the dialogs that follow with these, one each (game-machine-answers).
std::deque<std::string> test_answers;
bool TakeAnswer(std::string &answer)
{
    if (test_answers.empty())
        return false;
    answer = test_answers.front();
    test_answers.pop_front();
    return true;
}
// The question for programs opened in the Finder is on.
bool ask_on = true;
// No window server session (the dummy driver of the tests): no panel shows.
bool Screen()
{
    const char *driver = SDL_GetCurrentVideoDriver();
    return driver && !strcmp(driver, "cocoa");
}
NSString *Text(const std::string &text) { return [NSString stringWithUTF8String:text.c_str()] ?: @"?"; }
void Push(HostGameMenu::Request::Kind kind, const std::string &program, const std::string &name)
{
    HostGameMenu::Request request;
    request.kind = kind;
    request.program = program;
    request.name = name;
    queue.push_back(request);
}
void Rebuild();
} // namespace

@interface NEO98GameMenuTarget : NSObject <NSMenuDelegate>
- (void)choose:(NSMenuItem *)item;
@end

@implementation NEO98GameMenuTarget
- (void)choose:(NSMenuItem *)item
{
    using Kind = HostGameMenu::Request::Kind;
    NSString *name = item.identifier;
    const bool answered = test_answered;
    const std::string answer = test_answer;
    test_answered = false;
    const std::string chosen = answered ? answer : std::string([item.representedObject isKindOfClass:NSString.class]
                                                                   ? [(NSString *)item.representedObject UTF8String] : "");
    if ([name isEqualToString:@"game-machine-create"])
    {
        std::string program, wanted;
        if (answered)
        {
            const auto bar = answer.find('|');
            program = answer.substr(0, bar);
            wanted = bar == std::string::npos ? std::string() : answer.substr(bar + 1);
        }
        else if (Screen())
        {
            NSOpenPanel *panel = [NSOpenPanel openPanel];
            panel.title = @"Choose a program for the game machine";
            panel.message = @"Choose the game's executable on drive C:. The game machine runs only this "
                            @"program and keeps its saved files separate.";
            panel.prompt = @"Choose";
            panel.canChooseDirectories = NO;
            panel.canChooseFiles = YES;
            panel.allowsMultipleSelection = NO;
            if (!drive_folder.empty())
                panel.directoryURL = [NSURL fileURLWithPath:Text(drive_folder) isDirectory:YES];
            if ([panel runModal] == NSModalResponseOK && panel.URL.fileSystemRepresentation)
                program = panel.URL.fileSystemRepresentation;
            // The panel ate the key-up and mouse-up of the click that closed it.
            SDL_ResetKeyboard();
        }
        if (!program.empty())
            Push(Kind::Create, program, wanted);
    }
    else if ([name isEqualToString:@"game-machine-create-front"])
        Push(Kind::CreateFront, std::string(), answered ? answer : std::string());
    else if ([name isEqualToString:@"game-machine-start"])
        Push(Kind::Start, std::string(), chosen);
    else if ([name isEqualToString:@"game-machine-reveal"])
        Push(Kind::Reveal, std::string(), chosen);
    else if ([name isEqualToString:@"game-machine-delete"])
        Push(Kind::Delete, std::string(), chosen);
    else if ([name isEqualToString:@"game-machine-desktop"])
    {
        // A test names the folder for the alias behind the name.
        const auto bar = chosen.find('|');
        Push(Kind::Desktop, bar == std::string::npos ? std::string() : chosen.substr(bar + 1), chosen.substr(0, bar));
    }
    else if ([name isEqualToString:@"game-machine-ask"])
        Push(Kind::Ask, std::string(), std::string());
}
// The list is read when the menu opens: a game machine may have been made,
// started or ended by another host meanwhile.
- (void)menuNeedsUpdate:(NSMenu *)menu
{
    if (menu == games_menu)
        Rebuild();
}
@end

// The entry "Open as game machine" in the context menu of the Finder: the
// service NSServices of Info.plist names (tools/mac_app.py). macOS starts
// NEO98 for it when it does not run and sends this message with the files.
@interface NEO98GameService : NSObject
- (void)openAsGameMachine:(NSPasteboard *)pasteboard userData:(NSString *)data error:(NSString **)error;
@end

@implementation NEO98GameService
- (void)openAsGameMachine:(NSPasteboard *)pasteboard userData:(NSString *)data error:(NSString **)error
{
    NSArray<NSURL *> *urls = [pasteboard readObjectsForClasses:@[ NSURL.class ]
                                                       options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
    for (NSURL *url in urls)
        if (url.fileSystemRepresentation)
            Push(HostGameMenu::Request::Kind::Open, url.fileSystemRepresentation, std::string());
}
@end

namespace
{
NEO98GameMenuTarget *target;
NEO98GameService *service;
NSMenuItem *Item(NSMenu *menu, NSString *title, NSString *name, const std::string &machine = std::string())
{
    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title action:@selector(choose:) keyEquivalent:@""];
    item.identifier = name;
    item.target = target;
    if (!machine.empty())
        item.representedObject = Text(machine);
    [menu addItem:item];
    return item;
}
void Rebuild()
{
    if (!games_menu)
        return;
    [games_menu removeAllItems];
    Item(games_menu, @"New game machine for a program …", @"game-machine-create");
    NSMenuItem *front = Item(games_menu,
                             front_program.empty() ? @"New game machine for the foreground program"
                                                   : Text("New game machine for \"" + front_program + "\" …"),
                             @"game-machine-create-front");
    front.enabled = !front_program.empty();
    const auto machines = HostGameMachine::List(machine_folder);
    if (!machines.empty())
        [games_menu addItem:[NSMenuItem separatorItem]];
    for (const auto &machine : machines)
    {
        NSMenuItem *holder = [[NSMenuItem alloc] initWithTitle:Text(machine.running ? machine.name + " (running)" : machine.name)
                                                        action:nil
                                                 keyEquivalent:@""];
        NSMenu *menu = [[NSMenu alloc] initWithTitle:Text(machine.name)];
        menu.autoenablesItems = NO;
        holder.submenu = menu;
        [games_menu addItem:holder];
        // Whose game machine it is: several may run the same program.
        if (@available(macOS 14.4, *))
            holder.subtitle = Text(machine.program);
        NSMenuItem *program = [[NSMenuItem alloc] initWithTitle:Text("Program: " + machine.program) action:nil keyEquivalent:@""];
        program.enabled = NO;
        [menu addItem:program];
        // Its saved states : they go with it when it is deleted.
        if (const unsigned states = HostSaveState::Count(machine.folder))
        {
            NSMenuItem *saved = [[NSMenuItem alloc] initWithTitle:Text(states == 1 ? std::string("1 saved state")
                                                                                   : std::to_string(states) + " saved states")
                                                           action:nil
                                                    keyEquivalent:@""];
            saved.enabled = NO;
            saved.identifier = @"game-machine-states";
            [menu addItem:saved];
        }
        [menu addItem:[NSMenuItem separatorItem]];
        // A game machine that runs comes to the front.
        Item(menu, machine.running ? @"Bring to front" : @"Start", @"game-machine-start", machine.name);
        Item(menu, @"Show in Finder", @"game-machine-reveal", machine.name);
        Item(menu, @"Create desktop launcher", @"game-machine-desktop", machine.name);
        [menu addItem:[NSMenuItem separatorItem]];
        Item(menu, @"Delete …", @"game-machine-delete", machine.name).enabled = !machine.running;
    }
    [games_menu addItem:[NSMenuItem separatorItem]];
    Item(games_menu, @"Ask when opening from Finder", @"game-machine-ask").state =
        ask_on ? NSControlStateValueOn : NSControlStateValueOff;
}
} // namespace

namespace HostGameMenu
{
bool Install(const std::string &machine, const std::string &drive_c)
{
    @autoreleasepool
    {
        if (games_menu)
            return true;
        if (!HostMenu::Installed() || machine.empty())
            return false;
        NSMenu *owner = [NSApp.mainMenu itemWithTitle:@"Machine"].submenu;
        if (!owner)
            return false;
        machine_folder = machine;
        drive_folder = drive_c;
        target = [NEO98GameMenuTarget new];
        [owner addItem:[NSMenuItem separatorItem]];
        NSMenuItem *holder = [[NSMenuItem alloc] initWithTitle:@"Game machines" action:nil keyEquivalent:@""];
        games_menu = [[NSMenu alloc] initWithTitle:@"Game machines"];
        games_menu.autoenablesItems = NO;
        games_menu.delegate = target;
        holder.submenu = games_menu;
        [owner addItem:holder];
        Rebuild();
        InstallService();
        return true;
    }
}

bool InstallService()
{
    @autoreleasepool
    {
        if (service)
            return true;
        // The service of the Finder, for the app in its bundle only: a host
        // of a checkout must not take the requests meant for the app.
        if (!Screen() || !NSApp || ![NSBundle.mainBundle.bundleIdentifier isEqualToString:@"at.andreas-horvath.neo98"])
            return false;
        service = [NEO98GameService new];
        NSApp.servicesProvider = service;
        // macOS reads the services of the bundle again, so the entry is
        // there after the first start without logging out.
        NSUpdateDynamicServices();
        return true;
    }
}

bool Waiting() { return !queue.empty(); }

void Alert(const std::string &title, const std::string &message)
{
    printf("NEO98 HOST notice %s: %s\n", title.c_str(), message.c_str());
    fflush(stdout);
    if (!Screen() || !NSApp)
        return;
    @autoreleasepool
    {
        NSAlert *alert = [NSAlert new];
        alert.messageText = Text(title);
        alert.informativeText = Text(message);
        [alert addButtonWithTitle:@"OK"];
        [alert runModal];
        SDL_ResetKeyboard();
    }
}

void Refresh(const std::string &front)
{
    @autoreleasepool
    {
        front_program = front;
        Rebuild();
    }
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

void Answer(const char *answer)
{
    test_answered = answer != nullptr;
    test_answer = answer ? answer : "";
}

void Answers(const char *answers)
{
    const std::string text = answers ? answers : "";
    for (size_t from = 0; from <= text.size();)
    {
        const size_t end = std::min(text.find('|', from), text.size());
        test_answers.push_back(text.substr(from, end - from));
        from = end + 1;
    }
}

void SetAsk(bool on)
{
    @autoreleasepool
    {
        ask_on = on;
        Rebuild();
    }
}

OpenAs AskOpen(const std::string &program, bool &never)
{
    never = false;
    if (std::string answer; TakeAnswer(answer))
    {
        if (const auto plus = answer.find("+never"); plus != std::string::npos)
        {
            never = true;
            answer.erase(plus);
        }
        return answer == "game" ? OpenAs::Game : answer == "normal" ? OpenAs::Normal : OpenAs::Cancel;
    }
    if (!Screen())
        return OpenAs::NotAsked;
    @autoreleasepool
    {
        NSAlert *alert = [NSAlert new];
        alert.messageText = Text("Open \"" + program + "\"");
        alert.informativeText = @"Normally, this program runs alongside other Windows 98 programs.\n\n"
                                @"A game machine gives it a separate window, private "
                                @"saves, pause controls and saved states.";
        [alert addButtonWithTitle:@"Open normally"];
        [alert addButtonWithTitle:@"Open as game machine"];
        [alert addButtonWithTitle:@"Cancel"];
        alert.showsSuppressionButton = YES;
        alert.suppressionButton.title = @"Do not show this dialog again";
        const NSModalResponse answer = [alert runModal];
        SDL_ResetKeyboard();
        if (answer != NSAlertFirstButtonReturn && answer != NSAlertSecondButtonReturn)
            return OpenAs::Cancel;
        never = alert.suppressionButton.state == NSControlStateValueOn;
        return answer == NSAlertSecondButtonReturn ? OpenAs::Game : OpenAs::Normal;
    }
}

bool Choose(const std::string &program, const std::vector<std::string> &names, std::string &chosen)
{
    chosen.clear();
    if (std::string answer; TakeAnswer(answer))
    {
        for (const auto &name : names)
            if (name == answer)
                chosen = name;
        return !chosen.empty();
    }
    if (!Screen() || names.empty())
        return false;
    @autoreleasepool
    {
        NSAlert *alert = [NSAlert new];
        alert.messageText = Text("Multiple game machines for \"" + program + "\"");
        alert.informativeText = @"Which one should start?";
        NSPopUpButton *list = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 320, 26) pullsDown:NO];
        for (const auto &name : names)
        {
            // Not addItemWithTitle: two names may read alike, and a name may
            // hold characters a menu takes for something else.
            NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:Text(name) action:nil keyEquivalent:@""];
            [list.menu addItem:item];
        }
        alert.accessoryView = list;
        [alert addButtonWithTitle:@"Start"];
        [alert addButtonWithTitle:@"Cancel"];
        const NSModalResponse answer = [alert runModal];
        SDL_ResetKeyboard();
        const NSInteger index = list.indexOfSelectedItem;
        if (answer != NSAlertFirstButtonReturn || index < 0 || size_t(index) >= names.size())
            return false;
        chosen = names[size_t(index)];
        return true;
    }
}

bool MakeAlias(const std::string &original, const std::string &directory, std::string &made, std::string &error)
{
    @autoreleasepool
    {
        made.clear();
        NSString *folder = directory.empty() ? nil : Text(directory);
        // The desktop of the person only where there is one to look at.
        if (!folder && Screen())
            folder = NSSearchPathForDirectoriesInDomains(NSDesktopDirectory, NSUserDomainMask, YES).firstObject;
        if (!folder)
        {
            error = "Desktop folder not found.";
            return false;
        }
        NSURL *source = [NSURL fileURLWithPath:Text(original)];
        NSError *failure = nil;
        NSData *bookmark = [source bookmarkDataWithOptions:NSURLBookmarkCreationSuitableForBookmarkFile
                            includingResourceValuesForKeys:nil
                                             relativeToURL:nil
                                                     error:&failure];
        if (!bookmark)
        {
            error = failure.localizedDescription.UTF8String ?: "The launcher is missing.";
            return false;
        }
        // "Name", "Name 2",...; an alias of this launcher that already lies
        // there is written again in its place.
        NSString *name = source.lastPathComponent.stringByDeletingPathExtension;
        for (int number = 1; number < 100; ++number)
        {
            NSString *leaf = number == 1 ? name : [NSString stringWithFormat:@"%@ %d", name, number];
            NSURL *target = [NSURL fileURLWithPath:[folder stringByAppendingPathComponent:leaf]];
            if ([NSFileManager.defaultManager fileExistsAtPath:target.path])
            {
                if (!HostGameMachine::SameFile(AliasTarget(target.fileSystemRepresentation), original))
                    continue;
                [NSFileManager.defaultManager removeItemAtURL:target error:nil];
            }
            if (![NSURL writeBookmarkData:bookmark toURL:target options:NSURLBookmarkCreationSuitableForBookmarkFile error:&failure])
            {
                error = failure.localizedDescription.UTF8String ?: "Could not write the alias.";
                return false;
            }
            made = target.fileSystemRepresentation;
            return true;
        }
        error = "Too many files with this name already exist in the folder.";
        return false;
    }
}

std::string AliasTarget(const std::string &alias)
{
    @autoreleasepool
    {
        NSURL *target = [NSURL URLByResolvingAliasFileAtURL:[NSURL fileURLWithPath:Text(alias)]
                                                    options:NSURLBookmarkResolutionWithoutUI | NSURLBookmarkResolutionWithoutMounting
                                                      error:nil];
        return target.fileSystemRepresentation ?: "";
    }
}

std::string ApplicationName(std::string *bundle)
{
    @autoreleasepool
    {
        if (bundle)
            *bundle = NSBundle.mainBundle.bundlePath.UTF8String ?: "";
        return NSRunningApplication.currentApplication.localizedName.UTF8String ?: "";
    }
}

bool AskName(const std::string &program, const std::string &suggested, std::string &name)
{
    name = suggested;
    if (std::string answer; TakeAnswer(answer))
    {
        if (answer == "cancel")
            return false;
        if (const std::string typed = HostGameMachine::SafeName(answer); !typed.empty())
            name = typed;
        return true;
    }
    if (!Screen())
        return true;
    @autoreleasepool
    {
        NSAlert *alert = [NSAlert new];
        alert.messageText = @"New game machine";
        alert.informativeText = Text("A separate machine for " + program + ".\n\nIt runs only this program in its "
                                     "own window. It reads the main machine's drive C: and keeps the game's saved files "
                                     "in the game machine.");
        NSTextField *field = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 320, 24)];
        field.stringValue = Text(suggested);
        field.placeholderString = @"Game machine name";
        alert.accessoryView = field;
        [alert addButtonWithTitle:@"Create"];
        [alert addButtonWithTitle:@"Cancel"];
        alert.window.initialFirstResponder = field;
        const NSModalResponse answer = [alert runModal];
        SDL_ResetKeyboard();
        if (answer != NSAlertFirstButtonReturn)
            return false;
        const std::string typed = HostGameMachine::SafeName(field.stringValue.UTF8String ?: "");
        if (!typed.empty())
            name = typed;
        return true;
    }
}

std::string DeleteQuestion(unsigned states)
{
    std::string text = "This deletes its drive C: and everything the game saved there: "
                       "saves, settings and high scores.";
    if (states)
        text += states == 1 ? " Its saved state will also be deleted."
                            : " Its " + std::to_string(states) + " saved states will also be deleted.";
    return text + " The main machine and the game installed there are unchanged.";
}

bool ConfirmDelete(const std::string &name, unsigned states)
{
    if (!Screen())
        return true;
    @autoreleasepool
    {
        NSAlert *alert = [NSAlert new];
        alert.alertStyle = NSAlertStyleWarning;
        alert.messageText = Text("Delete game machine \"" + name + "\"?");
        alert.informativeText = Text(DeleteQuestion(states));
        [alert addButtonWithTitle:@"Cancel"];
        [alert addButtonWithTitle:@"Delete"];
        if (@available(macOS 11.0, *))
            alert.buttons[1].hasDestructiveAction = YES;
        const NSModalResponse answer = [alert runModal];
        SDL_ResetKeyboard();
        return answer == NSAlertSecondButtonReturn;
    }
}

void Reveal(const std::string &path)
{
    if (!Screen())
        return;
    @autoreleasepool
    {
        [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[ [NSURL fileURLWithPath:Text(path)] ]];
    }
}

void SetApplicationIcon(const std::string &icns)
{
    if (!Screen() || !NSApp)
        return;
    @autoreleasepool
    {
        if (NSImage *image = [[NSImage alloc] initWithContentsOfFile:Text(icns)])
            NSApp.applicationIconImage = image;
    }
}
} // namespace HostGameMenu
