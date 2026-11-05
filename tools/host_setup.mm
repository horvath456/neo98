// SPDX-License-Identifier: GPL-2.0-or-later
// The setup assistant and the settings window of the Mac app; see
// tools/host_setup.h and docs/BUILDING.md.
#include "host_setup.h"
#include "host_product_key.h"
#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <dirent.h>
#include <string>
#include <vector>

namespace
{
NSString *Text(const std::string &text) { return [NSString stringWithUTF8String:text.c_str()] ?: @""; }
std::string Utf8(NSString *text) { return text ? std::string(text.UTF8String) : std::string(); }
void Say(const std::string &line)
{
    printf("NEO98 WIZARD %s\n", line.c_str());
    fflush(stdout);
}
bool Headless()
{
    const char *driver = getenv("SDL_VIDEODRIVER");
    return driver && !strcmp(driver, "dummy");
}
const char *const Tools[] = {"qemu-system-i386", "mformat", "mcopy", "mdel", "mmd", "mdir", "cabextract", "tesseract"};
const char *const BrewLine = "brew install qemu mtools cabextract tesseract";
std::vector<std::string> ToolDirectories()
{
    const char *other = getenv("NEO98_SETUP_TOOL_DIRS");
    std::string text = other ? other : "/opt/homebrew/bin:/usr/local/bin";
    std::vector<std::string> out;
    size_t at = 0;
    while (at <= text.size())
    {
        size_t end = text.find(':', at);
        if (end == std::string::npos)
            end = text.size();
        if (end > at)
            out.push_back(text.substr(at, end - at));
        at = end + 1;
    }
    return out;
}
std::vector<std::string> MissingTools()
{
    std::vector<std::string> missing;
    const auto directories = ToolDirectories();
    for (const char *tool : Tools)
    {
        bool found = false;
        for (const auto &directory : directories)
            if (!access((directory + "/" + tool).c_str(), X_OK))
                found = true;
        if (!found)
            missing.push_back(tool);
    }
    return missing;
}
// Where setup keeps the installed disk image until the copy is done.
std::string WorkDirectory() { return HostSettings::Directory() + "/setup"; }
// An installation that ran to its end and only waits for the copy: QEMU and
// the key are not needed again.
bool InstalledDiskWaiting()
{
    NSString *work = Text(WorkDirectory());
    for (NSString *name in [NSFileManager.defaultManager contentsOfDirectoryAtPath:work error:nil] ?: @[])
        if ([NSFileManager.defaultManager
                fileExistsAtPath:[[work stringByAppendingPathComponent:name] stringByAppendingPathComponent:@"state.json"]])
            return true;
    return false;
}
// tools/setup_win98.py: in the bundle under Resources/setup, in a checkout
// three folders above build/native-app/neo98-host.
std::string SetupScript()
{
    const std::string contents = HostSettings::BundleContents();
    if (!contents.empty())
        return contents + "/Resources/setup/tools/setup_win98.py";
    return HostSettings::Parent(HostSettings::Parent(HostSettings::Parent(HostSettings::Executable()))) +
           "/tools/setup_win98.py";
}
bool KeyLooksRight(NSString *key)
{
    unsigned count = 0;
    for (NSUInteger i = 0; i < key.length; ++i)
    {
        const unichar c = [key characterAtIndex:i];
        if (c == '-' || c == ' ')
            continue;
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')))
            return false;
        ++count;
    }
    return count == 25;
}
NSTextField *Label(NSView *view, NSString *text, NSRect frame, CGFloat size = 13, bool bold = false)
{
    NSTextField *label = [NSTextField wrappingLabelWithString:text];
    label.frame = frame;
    label.font = bold ? [NSFont boldSystemFontOfSize:size] : [NSFont systemFontOfSize:size];
    [view addSubview:label];
    return label;
}
NSTextField *Field(NSView *view, NSString *placeholder, NSRect frame)
{
    NSTextField *field = [[NSTextField alloc] initWithFrame:frame];
    field.placeholderString = placeholder;
    field.usesSingleLineMode = YES;
    field.cell.scrollable = YES;
    [view addSubview:field];
    return field;
}
NSButton *Button(NSView *view, NSString *title, id target, SEL action, NSRect frame)
{
    NSButton *button = [NSButton buttonWithTitle:title target:target action:action];
    button.frame = frame;
    [view addSubview:button];
    return button;
}
// Makes the bundle the program a double click on.exe,.com and.scr starts,
// as the "Open with" choice of the Finder does. The other document types of
// Info.plist have no other owner on a Mac. Not from a bare binary and not
// without a person, so a test never changes the Mac it runs on.
void ClaimFileTypes()
{
    NSURL *app = NSBundle.mainBundle.bundleURL;
    if (Headless() || ![app.pathExtension isEqualToString:@"app"] || !NSBundle.mainBundle.bundleIdentifier)
        return;
    LSRegisterURL((__bridge CFURLRef)app, true);
    for (NSString *extension in @[ @"exe", @"com", @"scr" ])
        if (UTType *type = [UTType typeWithFilenameExtension:extension])
            [NSWorkspace.sharedWorkspace setDefaultApplicationAtURL:app
                                                  toOpenContentType:type
                                                  completionHandler:^(NSError *error) {
                                                      Say("file type " + Utf8(extension) +
                                                          (error ? " refused: " + Utf8(error.localizedDescription)
                                                                 : " claimed"));
                                                  }];
}
// ~/Neo98, or ~/Windows 98 where that name is taken by something else (on a
// Mac ~/Neo98 is also ~/neo98, which may be a checkout of this project).
std::string FreeDefaultMachine()
{
    const std::string folder = HostSettings::DefaultMachine();
    if (HostSettings::Ready(folder))
        return folder;
    bool taken = false;
    if (DIR *directory = opendir(folder.c_str()))
    {
        while (const dirent *entry = readdir(directory))
            if (entry->d_name[0] != '.')
                taken = true;
        closedir(directory);
    }
    return taken ? HostSettings::Home() + "/Windows 98" : folder;
}
NSMenuItem *MenuItem(NSMenu *menu, NSString *title, SEL action, NSString *key)
{
    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:key];
    [menu addItem:item];
    return item;
}
// The menu bar while the assistant runs: quit, and the edit commands the text
// fields need for Cmd+V.
void AssistantMenu()
{
    NSMenu *bar = [[NSMenu alloc] initWithTitle:@"Neo98"];
    NSMenuItem *app = [[NSMenuItem alloc] initWithTitle:@"Neo98" action:nil keyEquivalent:@""];
    app.submenu = [[NSMenu alloc] initWithTitle:@"Neo98"];
    MenuItem(app.submenu, @"Quit Neo98", @selector(terminate:), @"q");
    [bar addItem:app];
    NSMenuItem *edit = [[NSMenuItem alloc] initWithTitle:@"Edit" action:nil keyEquivalent:@""];
    edit.submenu = [[NSMenu alloc] initWithTitle:@"Edit"];
    MenuItem(edit.submenu, @"Undo", @selector(undo:), @"z");
    MenuItem(edit.submenu, @"Redo", @selector(redo:), @"z").keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
    [edit.submenu addItem:NSMenuItem.separatorItem];
    MenuItem(edit.submenu, @"Cut", @selector(cut:), @"x");
    MenuItem(edit.submenu, @"Copy", @selector(copy:), @"c");
    MenuItem(edit.submenu, @"Paste", @selector(paste:), @"v");
    MenuItem(edit.submenu, @"Select All", @selector(selectAll:), @"a");
    [bar addItem:edit];
    [NSApp setMainMenu:bar];
}
} // namespace

// A file dropped anywhere on the assistant is the ISO.
@interface NEO98DropView : NSView
@property(copy) void (^dropped)(NSString *path);
@end
@implementation NEO98DropView
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender
{
    return NSDragOperationCopy;
}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender
{
    NSArray<NSURL *> *urls = [sender.draggingPasteboard readObjectsForClasses:@[ NSURL.class ]
                                                                    options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
    if (!urls.count || !self.dropped)
        return NO;
    self.dropped(urls[0].path);
    return YES;
}
@end

@interface NEO98Assistant : NSObject <NSWindowDelegate, NSApplicationDelegate, NSTextFieldDelegate>
{
  @public
    NSWindow *window;
    NSTextField *isoField, *keyField, *folderField, *toolsLabel, *statusLabel, *messageLabel;
    NSTextField *titleLabel, *descriptionLabel;
    NSMutableArray<NSView *> *formViews;
    NSButton *detailsButton;
    BOOL showingForm, showingDetails;
    NSButton *claimBox, *installButton, *existingButton, *recheckButton, *cancelButton, *isoButton, *folderButton;
    NSProgressIndicator *progress;
    NSImageView *previewView;
    NSTimer *previewTimer;
    NSString *previewDirectory, *previewPath;
    NSTextView *logView;
    NSScrollView *logScroll;
    NSTask *task;
    NSMutableData *partial;
    std::vector<std::string> steps;
    size_t nextStep;
    std::string cancelOn, machine;
    std::vector<std::string> opens;
    BOOL headless, success, cancelled, finished;
}
@end

@implementation NEO98Assistant
- (void)build:(const std::string &)folder
{
    const CGFloat w = 600, h = 500;
    window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, w, h)
                                         styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                   NSWindowStyleMaskMiniaturizable
                                           backing:NSBackingStoreBuffered
                                             defer:NO];
    window.title = @"Set up Neo98";
    window.delegate = self;
    window.releasedWhenClosed = NO;
    NEO98DropView *view = [[NEO98DropView alloc] initWithFrame:NSMakeRect(0, 0, w, h)];
    [view registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
    __weak NEO98Assistant *me = self;
    view.dropped = ^(NSString *path) {
        NEO98Assistant *strong = me;
        if (strong && !strong->task)
            strong->isoField.stringValue = path;
    };
    window.contentView = view;
    formViews = [NSMutableArray array];
    titleLabel = Label(view, @"Install Windows 98", NSMakeRect(24, h - 52, w - 48, 28), 20, true);
    descriptionLabel = Label(view,
          @"Install from an English Windows 98 SE full-version ISO, ZIP or 7z.",
          NSMakeRect(24, h - 100, w - 48, 34));
    const NSUInteger formStart = view.subviews.count;
    CGFloat y = h - 136;
    Label(view, @"Windows 98 SE image, choose a file or drop it here:", NSMakeRect(24, y, w - 48, 18));
    isoField = Field(view, @"ISO, ZIP or 7z file", NSMakeRect(24, y - 28, w - 48 - 110, 24));
    isoButton = Button(view, @"Choose…", self, @selector(chooseIso:), NSMakeRect(w - 24 - 104, y - 32, 104, 32));
    y -= 64;
    Label(view, @"Product key:", NSMakeRect(24, y, w - 48, 18));
    keyField = Field(view, @"", NSMakeRect(24, y - 28, 350, 24));
    keyField.font = [NSFont monospacedSystemFontOfSize:14 weight:NSFontWeightRegular];
    keyField.formatter = [NEO98ProductKeyFormatter new];
    keyField.delegate = self;
    keyField.placeholderString = @"25-character product key";
    y -= 64;
    Label(view, @"Install location:", NSMakeRect(24, y, w - 48, 18));
    folderField = Field(view, @"", NSMakeRect(24, y - 28, w - 48 - 110, 24));
    folderField.stringValue = Text(folder);
    folderButton = Button(view, @"Choose…", self, @selector(chooseFolder:), NSMakeRect(w - 24 - 104, y - 32, 104, 32));
    y -= 62;
    toolsLabel = Label(view, @"", NSMakeRect(24, y - 22, w - 48 - 150, 40), 12);
    recheckButton = Button(view, @"Check again", self, @selector(recheck:), NSMakeRect(w - 24 - 140, y - 18, 140, 32));
    claimBox = [NSButton checkboxWithTitle:@"Open Windows programs with Neo98"
                                    target:nil
                                    action:nil];
    claimBox.frame = NSMakeRect(22, 116, w - 48, 20);
    claimBox.state = NSControlStateValueOn;
    [view addSubview:claimBox];
    [formViews addObjectsFromArray:[view.subviews subarrayWithRange:NSMakeRange(formStart, view.subviews.count - formStart)]];
    // The progress page shares the window: status line, bar, the lines of setup.
    statusLabel = Label(view, @"", NSMakeRect(24, h - 150, w - 48, 20), 13, true);
    progress = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(24, h - 178, w - 48, 16)];
    progress.indeterminate = YES;
    [view addSubview:progress];
    previewView = [[NSImageView alloc] initWithFrame:NSMakeRect(24, 192, w - 48, 364)];
    previewView.imageScaling = NSImageScaleProportionallyUpOrDown;
    previewView.imageAlignment = NSImageAlignCenter;
    [view addSubview:previewView];
    logScroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(24, 96, w - 48, h - 178 - 108)];
    logScroll.hasVerticalScroller = YES;
    logScroll.borderType = NSBezelBorder;
    logView = [[NSTextView alloc] initWithFrame:logScroll.bounds];
    logView.editable = NO;
    logView.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
    logView.autoresizingMask = NSViewWidthSizable;
    logScroll.documentView = logView;
    [view addSubview:logScroll];
    messageLabel = Label(view, @"", NSMakeRect(24, 62, w - 48, 40), 12);
    messageLabel.textColor = NSColor.systemRedColor;
    existingButton = Button(view, @"Use existing installation…", self, @selector(useExisting:),
                            NSMakeRect(18, 16, 210, 32));
    cancelButton = Button(view, @"Quit", self, @selector(cancel:), NSMakeRect(w - 24 - 220, 16, 100, 32));
    installButton = Button(view, @"Install", self, @selector(install:), NSMakeRect(w - 24 - 112, 16, 112, 32));
    detailsButton = Button(view, @"Show details", self, @selector(toggleDetails:), NSMakeRect(18, 16, 120, 32));
    cancelButton.keyEquivalent = @"\e";
    installButton.keyEquivalent = @"\r";
    [self showForm:YES];
    [self checkTools];
    [window center];
}
// Delete across automatic separators as one edit, preserving native undo.
- (BOOL)control:(NSControl *)control textView:(NSTextView *)editor doCommandBySelector:(SEL)command
{
    if (control != keyField || editor.selectedRange.length) return NO;
    const NSUInteger caret = editor.selectedRange.location;
    NSRange range = NSMakeRange(NSNotFound, 0);
    if (command == @selector(deleteBackward:) && caret >= 2 && [editor.string characterAtIndex:caret - 1] == '-')
        range = NSMakeRange(caret - 2, 2);
    else if (command == @selector(deleteForward:) && caret + 1 < editor.string.length && [editor.string characterAtIndex:caret] == '-')
        range = NSMakeRange(caret, 2);
    if (range.location == NSNotFound) return NO;
    [editor insertText:@"" replacementRange:range];
    return YES;
}
- (void)showForm:(BOOL)form
{
    showingForm = form;
    const CGFloat height = form ? 500 : (showingDetails ? 752 : 640);
    [window setContentSize:NSMakeSize(600, height)];
    titleLabel.frame = NSMakeRect(24, height - 52, 552, 28);
    descriptionLabel.frame = NSMakeRect(24, height - 100, 552, 34);
    descriptionLabel.stringValue = form
        ? @"Install from an English Windows 98 SE full-version ISO, ZIP or 7z."
        : @"Windows Setup will restart several times.";
    for (NSView *view in formViews) view.hidden = !form;
    recheckButton.hidden = !form || MissingTools().empty();
    statusLabel.frame = NSMakeRect(24, height - 146, 552, 20);
    progress.frame = NSMakeRect(24, height - 170, 552, 16);
    // A 4:3 screen with 24 points between it and the progress bar.
    previewView.frame = NSMakeRect(48, height - 572, 504, 378);
    logScroll.frame = NSMakeRect(24, 76, 552, 84);
    statusLabel.hidden = progress.hidden = previewView.hidden = form;
    logScroll.hidden = form || !showingDetails;
    messageLabel.hidden = !form;
    existingButton.hidden = installButton.hidden = !form;
    detailsButton.hidden = form;
    detailsButton.title = showingDetails ? @"Hide details" : @"Show details";
    cancelButton.title = form ? @"Quit" : @"Cancel";
    cancelButton.frame = NSMakeRect(form ? 344 : 464, 16, 112, 32);
    if (form) [progress stopAnimation:nil];
    else [progress startAnimation:nil];
}
- (void)toggleDetails:(id)sender
{
    showingDetails = !showingDetails;
    [self showForm:showingForm];
}
- (bool)checkTools
{
    const auto missing = MissingTools();
    recheckButton.hidden = missing.empty();
    if (missing.empty())
    {
        toolsLabel.stringValue = @"Ready to install.";
        toolsLabel.textColor = NSColor.secondaryLabelColor;
        Say("tools ok");
        return true;
    }
    std::string names;
    for (const auto &name : missing)
        names += (names.empty() ? "" : " ") + name;
    toolsLabel.stringValue = Text("Missing: " + names);
    toolsLabel.textColor = NSColor.systemRedColor;
    Say("tools missing: " + names + "; " + BrewLine);
    return false;
}
// A double click on a program or `open program.exe` while the assistant shows:
// an ISO is the one to install from, anything else starts once the machine
// is set up (HostSetup::AssistantOpens).
- (void)application:(NSApplication *)application openURLs:(NSArray<NSURL *> *)urls
{
    for (NSURL *url in urls)
    {
        if (!url.isFileURL || !url.path)
            continue;
        if ([@[ @"iso", @"zip", @"7z" ] containsObject:url.pathExtension.lowercaseString])
        {
            if (!task)
                isoField.stringValue = url.path;
            continue;
        }
        opens.push_back(Utf8(url.path));
        Say("open " + Utf8(url.path) + " kept");
        messageLabel.textColor = NSColor.secondaryLabelColor;
        messageLabel.stringValue = [NSString
            stringWithFormat:@"%@ will open when Windows 98 is ready.", url.lastPathComponent];
    }
}
- (void)message:(const std::string &)text
{
    messageLabel.textColor = NSColor.systemRedColor;
    messageLabel.stringValue = Text(text);
    if (!text.empty())
        Say("message " + text);
}
- (void)recheck:(id)sender
{
    [self checkTools];
}
- (NSString *)choose:(NSString *)title files:(BOOL)files
{
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.title = title;
    panel.message = title;
    panel.prompt = @"Choose";
    panel.canChooseFiles = files;
    panel.canChooseDirectories = !files;
    panel.canCreateDirectories = YES;
    panel.allowsMultipleSelection = NO;
    return [panel runModal] == NSModalResponseOK ? panel.URL.path : nil;
}
- (void)chooseIso:(id)sender
{
    if (NSString *path = [self choose:@"Choose a Windows 98 SE image" files:YES])
        isoField.stringValue = path;
}
- (void)chooseFolder:(id)sender
{
    if (NSString *path = [self choose:@"Choose an install location" files:NO])
        folderField.stringValue = path;
}
- (void)useExisting:(id)sender
{
    if (NSString *path = [self choose:@"Choose an existing installation" files:NO])
        [self existing:Utf8(path)];
}
- (void)existing:(const std::string &)folder
{
    if (!HostSettings::Ready(folder))
    {
        Say("existing " + folder + " refused");
        [self message:"No Windows 98 installation found in: " + folder];
        return;
    }
    Say("existing " + folder + " accepted");
    machine = folder;
    [self end:YES];
}
- (void)install:(id)sender
{
    if (task)
        return;
    [self message:""];
    const std::string iso = Utf8([isoField.stringValue stringByExpandingTildeInPath]),
                      folder = Utf8([folderField.stringValue stringByExpandingTildeInPath]);
    if (iso.empty() || !HostSettings::Exists(iso))
        return [self message:"Choose a Windows 98 SE ISO, ZIP or 7z file."];
    if (folder.empty() || folder[0] != '/')
        return [self message:"Choose an install location."];
    if (HostSettings::Ready(folder))
        return [self existing:folder];
    const bool waiting = InstalledDiskWaiting();
    if (!waiting && ![self checkTools])
    {
        Say("install refused: tools");
        return [self message:std::string("Some required tools are missing. Run in Terminal: ") + BrewLine +
                             ", then click \"Check again\"."];
    }
    if (!waiting && !KeyLooksRight(keyField.stringValue))
    {
        Say("install refused: key");
        return [self message:"Enter the 25-character product key."];
    }
    if (!HostSettings::MakeDirectories(folder) || !HostSettings::MakeDirectories(WorkDirectory()))
        return [self message:"Could not create the folder: " + folder];
    machine = folder;
    cancelled = NO;
    partial = [NSMutableData data];
    logView.string = @"";
    statusLabel.stringValue = @"Starting installation…";
    [self showForm:NO];
    previewDirectory = [NSTemporaryDirectory() stringByAppendingPathComponent:[[NSUUID UUID] UUIDString]];
    if (![[NSFileManager defaultManager] createDirectoryAtPath:previewDirectory withIntermediateDirectories:NO
            attributes:@{NSFilePosixPermissions: @0700} error:nil])
    { [self showForm:YES]; return [self message:"Could not create the preview folder."]; }
    previewPath = [previewDirectory stringByAppendingPathComponent:@"live.png"];
    previewView.image = nil;
    task = [NSTask new];
    task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/python3"];
    // -B: no __pycache__ inside the signed bundle.
    task.arguments = @[
        @"-B", Text(SetupScript()), @"--user", @"--iso", Text(iso), @"--target", Text(folder + "/C"), @"--work",
        Text(WorkDirectory()), @"--key-stdin", @"--preview", previewPath
    ];
    std::string path;
    for (const auto &directory : ToolDirectories())
        path += directory + ":";
    task.environment = @{
        @"PATH" : Text(path + "/usr/bin:/bin:/usr/sbin:/sbin"),
        @"HOME" : Text(HostSettings::Home()),
        @"PYTHONDONTWRITEBYTECODE" : @"1",
        @"PYTHONUNBUFFERED" : @"1",
        @"LC_ALL" : @"en_US.UTF-8",
    };
    NSPipe *input = [NSPipe pipe], *output = [NSPipe pipe];
    task.standardInput = input;
    task.standardOutput = output;
    task.standardError = output;
    __weak NEO98Assistant *me = self;
    output.fileHandleForReading.readabilityHandler = ^(NSFileHandle *handle) {
        NSData *data = handle.availableData;
        if (data.length)
            dispatch_async(dispatch_get_main_queue(), ^{
                [me received:data];
            });
    };
    task.terminationHandler = ^(NSTask *ended) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [me ended:ended.terminationStatus];
        });
    };
    NSError *error = nil;
    if (![task launchAndReturnError:&error])
    {
        task = nil;
        [self stopPreview];
        [self showForm:YES];
        return [self message:"Could not start Python: " + Utf8(error.localizedDescription)];
    }
    previewTimer = [NSTimer scheduledTimerWithTimeInterval:0.5 repeats:YES block:^(NSTimer *) {
        NEO98Assistant *strong = me;
        if (!strong) return;
        NSData *data = [NSData dataWithContentsOfFile:strong->previewPath];
        NSImage *frame = data ? [[NSImage alloc] initWithData:data] : nil;
        if (frame) strong->previewView.image = frame;
    }];
    // The key goes down the pipe and nowhere else.
    NSString *key = [keyField.stringValue stringByAppendingString:@"\n"];
    @try
    {
        [input.fileHandleForWriting writeData:[key dataUsingEncoding:NSUTF8StringEncoding]];
        [input.fileHandleForWriting closeFile];
    }
    @catch (NSException *)
    {
    }
    Say("install started iso=" + iso + " folder=" + folder);
}
- (void)received:(NSData *)data
{
    [partial appendData:data];
    for (;;)
    {
        const char *bytes = (const char *)partial.bytes;
        const void *newline = memchr(bytes, '\n', partial.length);
        if (!newline)
            break;
        const NSUInteger length = (const char *)newline - bytes;
        std::string line(bytes, length);
        [partial replaceBytesInRange:NSMakeRange(0, length + 1) withBytes:nullptr length:0];
        [self line:line];
    }
}
- (void)line:(const std::string &)line
{
    if (line.empty())
        return;
    Say("status " + line);
    [logView.textStorage appendAttributedString:[[NSAttributedString alloc]
                                                    initWithString:[Text(line) stringByAppendingString:@"\n"]
                                                        attributes:@{
                                                            NSFontAttributeName : logView.font,
                                                            NSForegroundColorAttributeName : NSColor.textColor
                                                        }]];
    [logView scrollToEndOfDocument:nil];
    if (!line.compare(0, 7, "setup: "))
        statusLabel.stringValue = Text(line.substr(7));
    if (!cancelOn.empty() && line.find(cancelOn) != std::string::npos)
    {
        cancelOn.clear();
        [self cancel:nil];
    }
}
- (void)stopPreview
{
    [previewTimer invalidate]; previewTimer = nil;
    previewView.image = nil;
    if (previewDirectory) [[NSFileManager defaultManager] removeItemAtPath:previewDirectory error:nil];
    previewDirectory = nil; previewPath = nil;
}
- (void)ended:(int)status
{
    [self stopPreview];
    NSFileHandle *reading = [task.standardOutput fileHandleForReading];
    reading.readabilityHandler = nil;
    if (NSData *rest = [reading readDataToEndOfFile]; rest.length)
        [self received:rest];
    if (partial.length)
        [self line:std::string((const char *)partial.bytes, partial.length)];
    task = nil;
    Say("finished code=" + std::to_string(status));
    if (finished)
        return [self end:NO];
    if (status == 0 && HostSettings::Ready(machine))
        return [self end:YES];
    [self showForm:YES];
    if (cancelled)
    {
        [self message:"Installation cancelled."];
        messageLabel.textColor = NSColor.secondaryLabelColor;
    }
    else
    {
        NSString *last = [[logView.string componentsSeparatedByString:@"\n"]
            filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"length > 0"]]
                             .lastObject;
        [self message:"Installation failed. " + Utf8(last)];
    }
    [self runScript];
}
// Cancel during the installation stops setup, which removes what it made;
// Quit on the form leaves the app.
- (void)cancel:(id)sender
{
    if (task)
    {
        cancelled = YES;
        Say("cancel");
        statusLabel.stringValue = @"Cancelling…";
        [task terminate];
        return;
    }
    [self end:NO];
}
- (BOOL)windowShouldClose:(NSWindow *)sender
{
    if (task)
    {
        finished = YES;
        cancelled = YES;
        [task terminate];
        return NO;
    }
    [self end:NO];
    return NO;
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
    [self windowShouldClose:window];
    return NSTerminateCancel;
}
- (void)end:(BOOL)okay
{
    success = okay;
    finished = YES;
    [NSApp stop:nil];
    // stop: takes effect after the next event.
    [NSApp postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                        location:NSZeroPoint
                                   modifierFlags:0
                                       timestamp:0
                                    windowNumber:0
                                         context:nil
                                         subtype:0
                                           data1:0
                                           data2:0]
             atStart:YES];
}
// The steps of NEO98_HOST_WIZARD, up to one that waits for setup to end.
- (void)runScript
{
    while (!finished && !task && nextStep < steps.size())
    {
        const std::string step = steps[nextStep++];
        const auto equals = step.find('=');
        const std::string name = step.substr(0, equals), value = equals == std::string::npos ? "" : step.substr(equals + 1);
        if (name == "iso")
            isoField.stringValue = Text(value);
        else if (name == "key")
            keyField.stringValue = Text(value);
        else if (name == "folder")
            folderField.stringValue = Text(value);
        else if (name == "check")
            [self checkTools];
        else if (name == "cancel-on")
            cancelOn = value;
        else if (name == "install")
            [self install:nil];
        else if (name == "open")
            [self application:NSApp openURLs:@[ [NSURL fileURLWithPath:Text(value)] ]];
        else if (name == "existing")
            [self existing:value];
        else if (name == "quit")
            [self end:NO];
        if (name != "key")
            Say("step " + step);
    }
    // Without a person nobody presses Quit after the last step.
    if (headless && !finished && !task && nextStep >= steps.size())
        [self end:NO];
}
@end

namespace HostSetup
{
namespace
{
std::vector<std::string> assistant_opens;
}
const std::vector<std::string> &AssistantOpens() { return assistant_opens; }
bool RunAssistant(HostSettings::Settings &settings)
{
    @autoreleasepool
    {
        const bool headless = Headless();
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:headless ? NSApplicationActivationPolicyProhibited
                                            : NSApplicationActivationPolicyRegular];
        AssistantMenu();
        NEO98Assistant *assistant = [NEO98Assistant new];
        assistant->headless = headless;
        if (const char *script = getenv("NEO98_HOST_WIZARD"))
        {
            std::string text = script;
            size_t at = 0;
            while (at < text.size())
            {
                size_t end = text.find(';', at);
                if (end == std::string::npos)
                    end = text.size();
                if (end > at)
                    assistant->steps.push_back(text.substr(at, end - at));
                at = end + 1;
            }
        }
        [assistant build:settings.machine.empty() ? FreeDefaultMachine() : settings.machine];
        NSApp.delegate = assistant;
        Say(std::string("shown settings=") + HostSettings::File());
        if (!headless)
        {
            [assistant->window makeKeyAndOrderFront:nil];
            [NSApp activateIgnoringOtherApps:YES];
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            [assistant runScript];
        });
        [NSApp run];
        [assistant->window orderOut:nil];
        NSApp.delegate = nil;
        if (!assistant->success)
        {
            Say("quit");
            return false;
        }
        settings.machine = assistant->machine;
        assistant_opens = assistant->opens;
        if (assistant->claimBox.state == NSControlStateValueOn)
            ClaimFileTypes();
        const bool saved = HostSettings::Save(settings);
        Say("ready machine=" + settings.machine + (saved ? "" : " (settings not written)"));
        return true;
    }
}

namespace
{
NSWindow *preferences;
std::string chosen_machine;
bool chosen = false, chosen_restart = false;
}
} // namespace HostSetup

@interface NEO98Preferences : NSObject
- (void)chooseMachine:(id)sender;
- (void)reveal:(id)sender;
- (void)close:(id)sender;
@property(copy) NSString *machine;
@end
@implementation NEO98Preferences
- (void)chooseMachine:(id)sender
{
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.title = panel.message = @"Choose another machine folder";
    panel.prompt = @"Choose";
    panel.canChooseFiles = NO;
    panel.canChooseDirectories = YES;
    panel.canCreateDirectories = YES;
    if ([panel runModal] != NSModalResponseOK)
        return;
    const std::string folder = Utf8(panel.URL.path);
    NSAlert *alert = [NSAlert new];
    if (HostSettings::Ready(folder))
    {
        alert.messageText = @"Machine folder changed";
        alert.informativeText = [NSString
            stringWithFormat:@"%@ will be drive C: after the next machine restart.", Text(HostSettings::DriveC(folder))];
    }
    else
    {
        alert.messageText = @"This folder has no Windows 98 installation";
        alert.informativeText = @"After restarting, the assistant will help you set up Windows in this folder.";
    }
    [alert addButtonWithTitle:@"Restart now"];
    [alert addButtonWithTitle:@"Later"];
    [alert addButtonWithTitle:@"Cancel"];
    const NSModalResponse answer = [alert runModal];
    if (answer == NSAlertThirdButtonReturn)
        return;
    HostSetup::chosen_machine = folder;
    HostSetup::chosen = true;
    HostSetup::chosen_restart = answer == NSAlertFirstButtonReturn;
    [HostSetup::preferences close];
}
- (void)reveal:(id)sender
{
    [NSWorkspace.sharedWorkspace selectFile:nil inFileViewerRootedAtPath:self.machine];
}
- (void)close:(id)sender
{
    [HostSetup::preferences close];
}
@end

namespace HostSetup
{
namespace
{
NEO98Preferences *preferences_target;
}

void ShowPreferences(const Info &info)
{
    std::string text;
    auto row = [&](const std::string &name, const std::string &value) { text += name + ": " + value + "\n"; };
    row("Machine folder", info.machine.empty() ? "(keiner)" : info.machine);
    row("Drive C", info.drive_c);
    for (char letter = 'D'; letter <= 'Z'; ++letter)
        if (!info.drives[letter - 'A'].empty())
            row(std::string("Drive ") + letter, info.drives[letter - 'A']);
    row("CD-ROM", info.cdrom.empty() ? "no media" : std::string(1, info.cdrom_letter ? info.cdrom_letter : '?') +
                                                               ": " + info.cdrom);
    row("Scale", info.fullscreen ? "Fullscreen" : std::to_string(info.scale) + "x");
    row("Network", info.online ? "verbunden" : "disconnected");
    row("Logs", info.logs);
    row("Settings file", info.settings_file);
    size_t at = 0;
    while (at < text.size())
    {
        const auto end = text.find('\n', at);
        printf("NEO98 HOST preferences %s\n", text.substr(at, end - at).c_str());
        at = end + 1;
    }
    fflush(stdout);
    if (Headless())
        return;
    @autoreleasepool
    {
        if (preferences)
            [preferences close];
        const CGFloat w = 620, h = 330;
        preferences = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, w, h)
                                                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                    backing:NSBackingStoreBuffered
                                                      defer:NO];
        preferences.title = @"Settings";
        preferences.releasedWhenClosed = NO;
        NSView *view = preferences.contentView;
        preferences_target = [NEO98Preferences new];
        preferences_target.machine = Text(info.machine);
        Label(view, @"Machine", NSMakeRect(24, h - 46, w - 48, 24), 16, true);
        NSTextField *body = Label(view, Text(text), NSMakeRect(24, 92, w - 48, h - 146), 12);
        body.selectable = YES;
        Label(view,
              @"Change drives, CD-ROM, scale and networking in the menu bar. Settings are saved here. "
              @"A new machine folder takes effect after restarting the machine.",
              NSMakeRect(24, 54, w - 48, 34), 11)
            .textColor = NSColor.secondaryLabelColor;
        Button(view, @"Choose another machine folder …", preferences_target, @selector(chooseMachine:),
               NSMakeRect(18, 12, 270, 32));
        Button(view, @"Show in Finder", preferences_target, @selector(reveal:), NSMakeRect(292, 12, 150, 32));
        Button(view, @"Close", preferences_target, @selector(close:), NSMakeRect(w - 24 - 110, 12, 110, 32))
            .keyEquivalent = @"\r";
        [preferences center];
        [NSApp activateIgnoringOtherApps:YES];
        [preferences makeKeyAndOrderFront:nil];
    }
}

bool TakeMachineChoice(std::string &machine, bool &restart)
{
    if (!chosen)
        return false;
    chosen = false;
    machine = chosen_machine;
    restart = chosen_restart;
    return true;
}

bool ChooseMachine(const std::string &folder)
{
    if (folder.empty() || folder[0] != '/')
        return false;
    chosen_machine = folder;
    chosen = true;
    chosen_restart = false;
    return true;
}
} // namespace HostSetup
