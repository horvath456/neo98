// SPDX-License-Identifier: GPL-2.0-or-later
// The window "States" of a game machine; see tools/host_state_window.h and
//
#include "host_state_window.h"
#import <AppKit/AppKit.h>
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <set>

namespace
{
using HostStateWindow::Model;
using HostStateWindow::Request;
using HostStateWindow::Row;

std::deque<Request> queue;
std::deque<std::string> test_answers;
Model model;
// One row of the list: "Now" on top, then the states, the newest first.
// The tree beside them: the lane of the row's dot, whether a line comes into
// it from above and leaves it below (to the state it came from), the lanes
// that end in this dot (branches that came from this state) and the lanes
// that only pass.
struct Line
{
    Row row;
    bool now = false;
    int lane = 0;
    bool top = false, bottom = false;
    std::vector<int> joins, through;
};
std::vector<Line> lines;
int lanes_used = 1;
// The id of the chosen row; empty for "Now".
std::string chosen;

// No window server session (the dummy driver of the tests): nothing shows.
bool Screen()
{
    const char *driver = SDL_GetCurrentVideoDriver();
    return driver && !strcmp(driver, "cocoa");
}
NSString *Text(const std::string &text) { return [NSString stringWithUTF8String:text.c_str()] ?: @"?"; }
std::string Plain(NSString *text) { return text.UTF8String ?: ""; }

void Arrange()
{
    lines.clear();
    std::vector<Row> rows = model.rows;
    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) { return a.time != b.time ? a.time > b.time : a.id > b.id; });
    std::set<std::string> known;
    Line now;
    now.now = true;
    now.row.name = "Now";
    for (const auto &row : rows)
    {
        known.insert(row.id);
        if (row.current)
            now.row.parent = row.id;
    }
    lines.push_back(now);
    for (const auto &row : rows)
    {
        Line line;
        line.row = row;
        lines.push_back(line);
    }
    // The lanes, from the top: each holds the id of the state its line leads
    // down to.
    std::vector<std::string> lanes;
    lanes_used = 1;
    for (auto &line : lines)
    {
        const std::string id = line.now ? std::string("\x01") : line.row.id;
        int mine = -1;
        for (size_t i = 0; i < lanes.size(); ++i)
            if (lanes[i] == id)
            {
                if (mine < 0)
                    mine = int(i);
                else
                    line.joins.push_back(int(i));
            }
        line.top = mine >= 0;
        if (mine < 0)
        {
            for (size_t i = 0; i < lanes.size() && mine < 0; ++i)
                if (lanes[i].empty())
                    mine = int(i);
            if (mine < 0)
            {
                lanes.push_back(std::string());
                mine = int(lanes.size()) - 1;
            }
        }
        for (size_t i = 0; i < lanes.size(); ++i)
            if (int(i) != mine && !lanes[i].empty() && lanes[i] != id)
                line.through.push_back(int(i));
        for (int join : line.joins)
            lanes[size_t(join)].clear();
        const bool parent = !line.row.parent.empty() && known.count(line.row.parent) && line.row.parent != line.row.id;
        lanes[size_t(mine)] = parent ? line.row.parent : std::string();
        line.bottom = parent;
        line.lane = mine;
        lanes_used = std::max(lanes_used, int(lanes.size()));
        while (!lanes.empty() && lanes.back().empty())
            lanes.pop_back();
    }
}
int ChosenIndex()
{
    for (size_t i = 0; i < lines.size(); ++i)
        if (lines[i].now ? chosen.empty() : lines[i].row.id == chosen)
            return int(i);
    return 0;
}
const Line *ChosenLine() { return lines.empty() ? nullptr : &lines[size_t(ChosenIndex())]; }

NSString *When(long long time, bool full)
{
    static NSDateFormatter *shortly, *fully;
    if (!shortly)
    {
        shortly = [NSDateFormatter new];
        shortly.locale = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
        shortly.dateStyle = NSDateFormatterMediumStyle;
        shortly.timeStyle = NSDateFormatterShortStyle;
        shortly.doesRelativeDateFormatting = YES;
        fully = [NSDateFormatter new];
        fully.locale = shortly.locale;
        fully.dateStyle = NSDateFormatterLongStyle;
        fully.timeStyle = NSDateFormatterMediumStyle;
    }
    return [(full ? fully : shortly) stringFromDate:[NSDate dateWithTimeIntervalSince1970:(NSTimeInterval)time]];
}
// What a row says below its name.
std::string Kind(const Row &row, bool full)
{
    std::string text = row.kind == "auto" ? (full ? "Saved automatically before loading another state" : "Saved automatically")
                     : row.kind == "quick" ? (full ? "Quick save (⇧⌘S)" : row.name == "Quick save" ? "" : "Quick save")
                     : full ? "Saved manually" : "";
    if (row.game == "off")
        text += std::string(text.empty() ? "" : full ? ". " : " · ") +
                (full ? "Files only: restart the game with its files from this state." : "Files only");
    return text;
}
NSImage *PictureOf(const Line &line)
{
    static NSMutableDictionary<NSString *, NSImage *> *cache;
    if (!cache)
        cache = [NSMutableDictionary new];
    if (line.now)
    {
        if (model.live.empty() || !model.live_width || !model.live_height)
            return nil;
        NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
                                                                        pixelsWide:model.live_width
                                                                        pixelsHigh:model.live_height
                                                                     bitsPerSample:8
                                                                   samplesPerPixel:3
                                                                          hasAlpha:NO
                                                                          isPlanar:NO
                                                                    colorSpaceName:NSDeviceRGBColorSpace
                                                                       bytesPerRow:model.live_width * 3
                                                                      bitsPerPixel:24];
        unsigned char *out = rep.bitmapData;
        for (size_t i = 0; i < model.live.size() && i < size_t(model.live_width) * model.live_height; ++i)
        {
            *out++ = (unsigned char)(model.live[i] >> 16);
            *out++ = (unsigned char)(model.live[i] >> 8);
            *out++ = (unsigned char)model.live[i];
        }
        NSImage *image = [[NSImage alloc] initWithSize:NSMakeSize(model.live_width, model.live_height)];
        [image addRepresentation:rep];
        return image;
    }
    if (line.row.picture.empty())
        return nil;
    NSString *path = Text(line.row.picture);
    NSImage *image = cache[path];
    if (!image)
    {
        image = [[NSImage alloc] initWithContentsOfFile:path];
        if (image)
            cache[path] = image;
    }
    return image;
}
} // namespace

namespace
{
// The window is being drawn into a picture: it is no key window then, and
// the chosen row is marked as in a list in front all the same.
bool picture_drawing = false;
}

// One row: the tree, the picture, the name, the time, and what is special
// about the state. The row marks itself when it is chosen (the table's own
// mark is not drawn into a picture of a window that is on no screen).
@interface NEO98StateCell : NSTableCellView
@property NSInteger index;
@end

@implementation NEO98StateCell
- (BOOL)isFlipped
{
    return YES;
}
- (void)drawRect:(NSRect)dirty
{
    if (self.index < 0 || size_t(self.index) >= lines.size())
        return;
    const Line &line = lines[size_t(self.index)];
    NSTableView *table = (NSTableView *)self.superview.superview;
    const bool selected = [table isKindOfClass:NSTableView.class] && table.selectedRow == self.index;
    // In front: the accent colour and white text. Behind another window: grey.
    const bool lit = selected && (picture_drawing || (self.window.keyWindow && self.window.firstResponder == table));
    if (selected)
    {
        [(lit ? NSColor.selectedContentBackgroundColor : NSColor.unemphasizedSelectedContentBackgroundColor) setFill];
        [[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds, 4, 1) xRadius:6 yRadius:6] fill];
    }
    const CGFloat height = self.bounds.size.height, width = self.bounds.size.width, middle = height / 2;
    const CGFloat step = 13, first = 16;
    const auto lane_x = [&](int lane) { return first + step * lane; };
    // The tree.
    NSColor *wire = lit ? [NSColor colorWithWhite:1 alpha:0.75] : NSColor.tertiaryLabelColor;
    [wire setStroke];
    const auto stroke = [&](NSPoint from, NSPoint to) {
        NSBezierPath *path = [NSBezierPath bezierPath];
        path.lineWidth = 2;
        path.lineCapStyle = NSLineCapStyleRound;
        [path moveToPoint:from];
        if (from.x == to.x)
            [path lineToPoint:to];
        else
            // A branch: down its own lane, then over into the dot.
            [path curveToPoint:to controlPoint1:NSMakePoint(from.x, to.y - 4) controlPoint2:NSMakePoint(from.x, to.y)];
        [path stroke];
    };
    for (int lane : line.through)
        stroke(NSMakePoint(lane_x(lane), 0), NSMakePoint(lane_x(lane), height));
    const CGFloat x = lane_x(line.lane);
    if (line.top)
        stroke(NSMakePoint(x, 0), NSMakePoint(x, middle));
    for (int lane : line.joins)
        stroke(NSMakePoint(lane_x(lane), 0), NSMakePoint(x, middle));
    if (line.bottom)
        stroke(NSMakePoint(x, middle), NSMakePoint(x, height));
    // The dot: "Now" a ring, the state the machine stands on in the accent
    // colour, an automatic one hollow, one that cannot be loaded grey.
    NSColor *accent = lit ? NSColor.whiteColor : NSColor.controlAccentColor;
    NSColor *plain = lit ? NSColor.whiteColor : NSColor.secondaryLabelColor;
    const CGFloat radius = line.now || line.row.current ? 5.5 : 4.5;
    NSBezierPath *dot = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(x - radius, middle - radius, 2 * radius, 2 * radius)];
    const bool hollow = line.now || line.row.kind == "auto";
    NSColor *colour = line.now || line.row.current ? accent : !line.row.usable ? (lit ? NSColor.whiteColor : NSColor.tertiaryLabelColor) : plain;
    if (hollow)
    {
        [(lit ? NSColor.selectedContentBackgroundColor : selected ? NSColor.unemphasizedSelectedContentBackgroundColor
                                                                    : NSColor.controlBackgroundColor) setFill];
        [dot fill];
        dot.lineWidth = 2;
        [colour setStroke];
        [dot stroke];
    }
    else
    {
        [colour setFill];
        [dot fill];
    }
    // The picture.
    const CGFloat left = first + step * std::min(lanes_used, 8) + 2;
    const NSRect frame = NSMakeRect(left, 6, (height - 12) * 1.6, height - 12);
    NSBezierPath *border = [NSBezierPath bezierPathWithRoundedRect:frame xRadius:4 yRadius:4];
    [NSGraphicsContext saveGraphicsState];
    [border addClip];
    [[NSColor colorWithWhite:0.1 alpha:1] setFill];
    NSRectFill(frame);
    if (NSImage *image = PictureOf(line))
    {
        // Fitted into the frame, its proportions kept.
        const NSSize size = image.size;
        const CGFloat scale = std::min(frame.size.width / std::max<CGFloat>(1, size.width), frame.size.height / std::max<CGFloat>(1, size.height));
        const NSRect fit = NSMakeRect(NSMidX(frame) - size.width * scale / 2, NSMidY(frame) - size.height * scale / 2, size.width * scale,
                                      size.height * scale);
        [image drawInRect:fit fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:line.now || line.row.usable ? 1.0 : 0.45
               respectFlipped:YES
                        hints:@{NSImageHintInterpolation : @(NSImageInterpolationHigh)}];
    }
    [NSGraphicsContext restoreGraphicsState];
    [(lit ? [NSColor colorWithWhite:1 alpha:0.5] : NSColor.separatorColor) setStroke];
    border.lineWidth = 1;
    [border stroke];
    // What is special about it, on the right.
    std::string badge;
    NSColor *badge_colour = nil;
    if (line.now)
        ;
    else if (line.row.damaged)
    {
        badge = "Damaged";
        badge_colour = NSColor.systemRedColor;
    }
    else if (!line.row.usable)
    {
        badge = "Incompatible";
        badge_colour = NSColor.systemOrangeColor;
    }
    else if (line.row.current)
    {
        badge = "Current";
        badge_colour = NSColor.controlAccentColor;
    }
    CGFloat right = width - 14;
    if (!badge.empty())
    {
        NSDictionary *attributes = @{
            NSFontAttributeName : [NSFont systemFontOfSize:10.5 weight:NSFontWeightSemibold],
            NSForegroundColorAttributeName : lit ? NSColor.selectedContentBackgroundColor : NSColor.whiteColor
        };
        const NSSize size = [Text(badge) sizeWithAttributes:attributes];
        const NSRect pill = NSMakeRect(right - size.width - 14, middle - 9, size.width + 14, 18);
        [(lit ? NSColor.whiteColor : badge_colour) setFill];
        [[NSBezierPath bezierPathWithRoundedRect:pill xRadius:9 yRadius:9] fill];
        [Text(badge) drawAtPoint:NSMakePoint(pill.origin.x + 7, pill.origin.y + (18 - size.height) / 2) withAttributes:attributes];
        right = pill.origin.x - 8;
    }
    // The name and what stands below it. A long name is cut at its end.
    const CGFloat text_left = NSMaxX(frame) + 12;
    NSMutableParagraphStyle *cut = [NSMutableParagraphStyle new];
    cut.lineBreakMode = NSLineBreakByTruncatingTail;
    const bool dim = !line.now && !line.row.usable;
    NSDictionary *name = @{
        NSFontAttributeName : [NSFont systemFontOfSize:13 weight:line.now ? NSFontWeightRegular : NSFontWeightSemibold],
        NSForegroundColorAttributeName : lit ? NSColor.whiteColor : dim ? NSColor.secondaryLabelColor : NSColor.labelColor,
        NSParagraphStyleAttributeName : cut
    };
    NSDictionary *below = @{
        NSFontAttributeName : [NSFont systemFontOfSize:11],
        NSForegroundColorAttributeName : lit ? [NSColor colorWithWhite:1 alpha:0.85] : NSColor.secondaryLabelColor,
        NSParagraphStyleAttributeName : cut
    };
    std::string second;
    if (line.now)
        second = "Current game state · not saved yet";
    else
    {
        second = Plain(When(line.row.time, false));
        if (const std::string kind = Kind(line.row, false); !kind.empty())
            second += " · " + kind;
    }
    [Text(line.row.name) drawInRect:NSMakeRect(text_left, middle - 18, std::max<CGFloat>(20, right - text_left), 18) withAttributes:name];
    [Text(second) drawInRect:NSMakeRect(text_left, middle + 1, std::max<CGFloat>(20, right - text_left), 16) withAttributes:below];
}
@end

// The table takes Return and the delete key as its buttons do.
@interface NEO98StateTable : NSTableView
@end
@implementation NEO98StateTable
- (void)keyDown:(NSEvent *)event
{
    NSString *keys = event.charactersIgnoringModifiers;
    const unichar key = keys.length ? [keys characterAtIndex:0] : 0;
    if (key == NSDeleteCharacter || key == NSBackspaceCharacter || key == NSDeleteFunctionKey)
        [NSApp sendAction:@selector(remove:) to:self.delegate from:self];
    else if (key == '\r' || key == 3)
        [NSApp sendAction:@selector(primary:) to:self.delegate from:self];
    else
        [super keyDown:event];
}
@end

// The ground of the window, drawn by the view itself so a picture of the
// window has it too.
@interface NEO98StateGround : NSView
@end
@implementation NEO98StateGround
- (void)drawRect:(NSRect)dirty
{
    [NSColor.windowBackgroundColor setFill];
    NSRectFill(dirty);
}
@end

@interface NEO98StateController : NSObject <NSTableViewDataSource, NSTableViewDelegate, NSWindowDelegate, NSTextFieldDelegate>
@property NSWindow *window;
@property NSTableView *table;
@property NSImageView *preview;
@property NSTextField *nameField, *whenLabel, *kindLabel, *fromLabel, *madeLabel, *reasonLabel, *statusLabel;
@property NSButton *primaryButton, *removeButton, *keepButton, *closeButton;
@property BOOL closing, arranging;
- (void)build;
- (void)reload;
- (void)show;
- (void)redraw;
- (void)primary:(id)sender;
- (void)remove:(id)sender;
- (void)keep:(id)sender;
- (void)renamed:(id)sender;
- (void)done:(id)sender;
@end

namespace
{
NEO98StateController *controller;
NSTextField *Label(NSView *into, NSRect frame, CGFloat size, NSColor *colour, bool wraps)
{
    NSTextField *label = wraps ? [NSTextField wrappingLabelWithString:@""] : [NSTextField labelWithString:@""];
    label.frame = frame;
    label.font = [NSFont systemFontOfSize:size];
    label.textColor = colour;
    label.selectable = NO;
    if (!wraps)
        label.lineBreakMode = NSLineBreakByTruncatingTail;
    label.autoresizingMask = NSViewMinXMargin | NSViewMinYMargin;
    [into addSubview:label];
    return label;
}
NSButton *Button(NSView *into, NSRect frame, NSString *title, id target, SEL action)
{
    NSButton *button = [NSButton buttonWithTitle:title target:target action:action];
    button.frame = frame;
    button.autoresizingMask = NSViewMinXMargin | NSViewMinYMargin;
    [into addSubview:button];
    return button;
}
// The question before a state is deleted. Without a screen the answer is
// yes, unless a test has queued another.
bool ConfirmDelete(const Line &line)
{
    if (!test_answers.empty())
    {
        const std::string answer = test_answers.front();
        test_answers.pop_front();
        return answer == "yes";
    }
    if (!Screen())
        return true;
    unsigned children = 0;
    for (const auto &other : lines)
        children += !other.now && other.row.parent == line.row.id ? 1u : 0u;
    NSAlert *alert = [NSAlert new];
    alert.alertStyle = NSAlertStyleWarning;
    alert.messageText = Text("Delete state \"" + line.row.name + "\"?");
    alert.informativeText = children ? @"This permanently deletes the state. Its child states remain "
                                       @"and will be attached to its parent."
                                     : @"This permanently deletes the state. The running game and other states are unchanged.";
    [alert addButtonWithTitle:@"Cancel"];
    [alert addButtonWithTitle:@"Delete"];
    if (@available(macOS 11.0, *))
        alert.buttons[1].hasDestructiveAction = YES;
    const NSModalResponse answer = [alert runModal];
    SDL_ResetKeyboard();
    return answer == NSAlertSecondButtonReturn;
}
} // namespace

@implementation NEO98StateController
- (void)build
{
    const CGFloat width = 900, height = 580, margin = 20, side = 300;
    NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, width, height)
                                                   styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable
                                                     backing:NSBackingStoreBuffered
                                                       defer:YES];
    window.releasedWhenClosed = NO;
    window.delegate = self;
    window.minSize = NSMakeSize(760, 460);
    // Over a game in full screen as well.
    window.collectionBehavior = NSWindowCollectionBehaviorFullScreenAuxiliary | NSWindowCollectionBehaviorMoveToActiveSpace;
    window.level = NSFloatingWindowLevel;
    self.window = window;
    window.contentView = [[NEO98StateGround alloc] initWithFrame:NSMakeRect(0, 0, width, height)];
    NSView *content = window.contentView;

    // The list.
    NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(margin, 56, width - 3 * margin - side, height - 56 - margin)];
    scroll.hasVerticalScroller = YES;
    scroll.borderType = NSBezelBorder;
    scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    NEO98StateTable *table = [[NEO98StateTable alloc] initWithFrame:scroll.bounds];
    NSTableColumn *column = [[NSTableColumn alloc] initWithIdentifier:@"state"];
    column.resizingMask = NSTableColumnAutoresizingMask;
    column.width = scroll.bounds.size.width - 4;
    [table addTableColumn:column];
    table.headerView = nil;
    table.rowHeight = 58;
    table.intercellSpacing = NSMakeSize(0, 0);
    table.allowsMultipleSelection = NO;
    table.allowsEmptySelection = NO;
    table.selectionHighlightStyle = NSTableViewSelectionHighlightStyleNone;
    table.backgroundColor = NSColor.controlBackgroundColor;
    table.columnAutoresizingStyle = NSTableViewUniformColumnAutoresizingStyle;
    if (@available(macOS 11.0, *))
        table.style = NSTableViewStyleFullWidth;
    table.dataSource = self;
    table.delegate = self;
    table.target = self;
    table.doubleAction = @selector(doubled:);
    scroll.documentView = table;
    [content addSubview:scroll];
    self.table = table;

    // The chosen state, large.
    const CGFloat x = width - margin - side;
    CGFloat y = height - margin - 188;
    NSImageView *preview = [[NSImageView alloc] initWithFrame:NSMakeRect(x, y, side, 188)];
    preview.imageScaling = NSImageScaleProportionallyUpOrDown;
    preview.wantsLayer = YES;
    preview.layer.backgroundColor = [NSColor colorWithWhite:0.1 alpha:1].CGColor;
    preview.layer.cornerRadius = 6;
    preview.layer.masksToBounds = YES;
    preview.autoresizingMask = NSViewMinXMargin | NSViewMinYMargin;
    [content addSubview:preview];
    self.preview = preview;
    y -= 36;
    NSTextField *name = [[NSTextField alloc] initWithFrame:NSMakeRect(x, y, side, 24)];
    name.font = [NSFont systemFontOfSize:13 weight:NSFontWeightSemibold];
    name.lineBreakMode = NSLineBreakByTruncatingTail;
    name.usesSingleLineMode = YES;
    name.delegate = self;
    name.target = self;
    name.action = @selector(renamed:);
    name.autoresizingMask = NSViewMinXMargin | NSViewMinYMargin;
    [content addSubview:name];
    self.nameField = name;
    y -= 24;
    self.whenLabel = Label(content, NSMakeRect(x, y, side, 18), 12, NSColor.labelColor, false);
    y -= 34;
    self.kindLabel = Label(content, NSMakeRect(x, y, side, 30), 11, NSColor.secondaryLabelColor, true);
    y -= 20;
    self.fromLabel = Label(content, NSMakeRect(x, y, side, 16), 11, NSColor.secondaryLabelColor, false);
    y -= 18;
    self.madeLabel = Label(content, NSMakeRect(x, y, side, 16), 11, NSColor.tertiaryLabelColor, false);
    y -= 98;
    self.reasonLabel = Label(content, NSMakeRect(x, y, side, 90), 11.5, NSColor.systemOrangeColor, true);
    y -= 42;
    self.primaryButton = Button(content, NSMakeRect(x - 6, y, side / 2 + 6, 32), @"Load", self, @selector(primary:));
    self.primaryButton.keyEquivalent = @"\r";
    self.removeButton = Button(content, NSMakeRect(x + side / 2, y, side / 2 + 6, 32), @"Delete …", self, @selector(remove:));
    y -= 34;
    self.keepButton = Button(content, NSMakeRect(x - 6, y, side + 12, 32), @"Keep (exclude from automatic cleanup)", self, @selector(keep:));

    // Below: what the window means for the game, and the way back.
    self.statusLabel = Label(content, NSMakeRect(margin, 12, width - 3 * margin - 190, 30), 11, NSColor.secondaryLabelColor, true);
    self.statusLabel.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;
    self.closeButton = Button(content, NSMakeRect(width - margin - 176, 12, 182, 32), @"Back to game", self, @selector(done:));
    self.closeButton.keyEquivalent = @"\033";
    self.closeButton.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;
    [window center];
}
- (void)reload
{
    Arrange();
    self.arranging = YES;
    [self.table reloadData];
    if (lines.empty())
    {
        self.arranging = NO;
        return;
    }
    // A chosen state that is gone: "Now".
    const NSInteger index = ChosenIndex();
    if (lines[size_t(index)].now)
        chosen.clear();
    [self.table selectRowIndexes:[NSIndexSet indexSetWithIndex:NSUInteger(index)] byExtendingSelection:NO];
    [self.table scrollRowToVisible:index];
    self.arranging = NO;
    self.window.title = Text(model.game.empty() ? std::string("States") : "States: " + model.game);
    unsigned automatic = 0;
    for (const auto &row : model.rows)
        automatic += row.kind == "auto" ? 1u : 0u;
    std::string status = model.rows.empty() ? "No saved states yet."
                       : std::to_string(model.rows.size()) + (model.rows.size() == 1 ? " state" : " states");
    if (automatic)
        status += ", " + std::to_string(automatic) + " automatic (keeping the latest " +
                  std::to_string(model.keep_automatic) + ")";
    if (!model.rows.empty())
        status += ".";
    status += " The game is paused while this window is open.";
    self.statusLabel.stringValue = Text(status);
    [self show];
}
// The right side for the chosen row.
- (void)show
{
    const Line *line = ChosenLine();
    if (!line)
        return;
    self.preview.image = PictureOf(*line);
    if (line->now)
    {
        self.nameField.stringValue = @"";
        self.nameField.placeholderString = @"New state name (optional)";
        self.nameField.enabled = model.can_save;
        self.whenLabel.stringValue = @"Current game state";
        self.madeLabel.stringValue = @"";
        self.kindLabel.stringValue = @"Not saved yet";
        std::string from;
        for (const auto &other : lines)
            if (!other.now && other.row.id == line->row.parent)
                from = "Resuming from: “" + other.row.name + "“";
        self.fromLabel.stringValue = Text(from);
        self.reasonLabel.stringValue =
            Text(model.can_save ? std::string("A state captures the running game and all files it has written. "
                                              "You can resume it later as often as you like.")
                                : "Cannot save a state right now. " + model.save_reason);
        self.reasonLabel.textColor = model.can_save ? NSColor.secondaryLabelColor : NSColor.systemOrangeColor;
        self.primaryButton.title = @"Save state";
        self.primaryButton.enabled = model.can_save;
        self.primaryButton.toolTip = model.can_save ? nil : Text(model.save_reason);
        self.removeButton.hidden = YES;
        self.keepButton.hidden = YES;
        return;
    }
    const Row &row = line->row;
    self.nameField.stringValue = Text(row.name);
    self.nameField.placeholderString = @"Name";
    self.nameField.enabled = !row.damaged;
    self.whenLabel.stringValue = When(row.time, true);
    self.kindLabel.stringValue = Text(Kind(row, true));
    self.madeLabel.stringValue = Text(row.revision.empty() ? std::string() : "Written by Neo98 " + row.revision);
    std::string from = "First state in this branch";
    for (const auto &other : lines)
        if (!other.now && other.row.id == row.parent && other.row.id != row.id)
            from = "Created from: “" + other.row.name + "“";
    self.fromLabel.stringValue = Text(from);
    self.reasonLabel.stringValue = Text(row.usable ? std::string() : row.reason);
    self.reasonLabel.textColor = row.damaged ? NSColor.systemRedColor : NSColor.systemOrangeColor;
    self.primaryButton.title = @"Load";
    self.primaryButton.enabled = row.usable;
    self.primaryButton.toolTip = row.usable ? @"Resume here. The current state is saved automatically first."
                                            : Text(row.reason);
    self.removeButton.hidden = NO;
    self.keepButton.hidden = row.kind != "auto" || row.damaged;
}
- (NSInteger)numberOfRowsInTableView:(NSTableView *)table
{
    return NSInteger(lines.size());
}
- (NSView *)tableView:(NSTableView *)table viewForTableColumn:(NSTableColumn *)column row:(NSInteger)row
{
    NEO98StateCell *cell = [table makeViewWithIdentifier:@"state" owner:self];
    if (!cell)
    {
        cell = [[NEO98StateCell alloc] initWithFrame:NSMakeRect(0, 0, column.width, table.rowHeight)];
        cell.identifier = @"state";
    }
    cell.index = row;
    cell.toolTip = row >= 0 && size_t(row) < lines.size() && !lines[size_t(row)].now ? Text(lines[size_t(row)].row.name) : nil;
    cell.needsDisplay = YES;
    return cell;
}
- (void)tableViewSelectionDidChange:(NSNotification *)notification
{
    // While the list is put together again the table chooses for itself.
    if (self.arranging)
        return;
    const NSInteger row = self.table.selectedRow;
    if (row < 0 || size_t(row) >= lines.size())
        return;
    chosen = lines[size_t(row)].now ? std::string() : lines[size_t(row)].row.id;
    [self redraw];
    [self show];
}
// Every row again: which one is chosen, and whether the window is in front.
- (void)redraw
{
    [self.table enumerateAvailableRowViewsUsingBlock:^(NSTableRowView *row, NSInteger index) {
        for (NSView *cell in row.subviews)
            cell.needsDisplay = YES;
    }];
}
- (void)windowDidBecomeKey:(NSNotification *)notification
{
    [self redraw];
}
- (void)windowDidResignKey:(NSNotification *)notification
{
    [self redraw];
}
- (void)doubled:(id)sender
{
    // On a state: load it. On "Now" a double click does nothing; saving
    // has its button.
    const NSInteger row = self.table.clickedRow;
    if (row < 0 || size_t(row) >= lines.size() || lines[size_t(row)].now)
        return;
    [self primary:sender];
}
- (void)primary:(id)sender
{
    const Line *line = ChosenLine();
    if (!line || !self.primaryButton.enabled)
    {
        NSBeep();
        return;
    }
    Request request;
    if (line->now)
    {
        request.kind = Request::Save;
        request.name = Plain(self.nameField.stringValue);
        queue.push_back(request);
        return;
    }
    request.kind = Request::Load;
    request.id = line->row.id;
    queue.push_back(request);
}
- (void)remove:(id)sender
{
    const Line *line = ChosenLine();
    if (!line || line->now)
        return;
    const Line copy = *line;
    if (!ConfirmDelete(copy))
        return;
    Request request;
    request.kind = Request::Delete;
    request.id = copy.row.id;
    queue.push_back(request);
}
- (void)keep:(id)sender
{
    const Line *line = ChosenLine();
    if (!line || line->now || line->row.kind != "auto")
        return;
    Request request;
    request.kind = Request::Keep;
    request.id = line->row.id;
    queue.push_back(request);
}
// The name field: Return or leaving it names the state; on "Now" Return saves.
- (void)renamed:(id)sender
{
    const Line *line = ChosenLine();
    if (!line)
        return;
    if (line->now)
    {
        if (sender == self.nameField && NSApp.currentEvent.type == NSEventTypeKeyDown)
            [self primary:sender];
        return;
    }
    const std::string name = Plain([self.nameField.stringValue stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet]);
    if (name.empty() || name == line->row.name)
    {
        self.nameField.stringValue = Text(line->row.name);
        return;
    }
    Request request;
    request.kind = Request::Rename;
    request.id = line->row.id;
    request.name = name;
    queue.push_back(request);
}
- (void)done:(id)sender
{
    [self.window performClose:sender];
}
- (void)windowWillClose:(NSNotification *)notification
{
    if (self.closing)
        return;
    Request request;
    request.kind = Request::Close;
    queue.push_back(request);
}
@end

namespace HostStateWindow
{
bool Open(const Model &given)
{
    @autoreleasepool
    {
        if (!NSApp)
            return false;
        if (!controller)
        {
            controller = [NEO98StateController new];
            [controller build];
        }
        model = given;
        // The window opens on "Now": Return there saves a state, which
        // loses nothing; on a state Return would load it.
        chosen.clear();
        [controller reload];
        if (Screen())
        {
            [NSApp activateIgnoringOtherApps:YES];
            [controller.window makeKeyAndOrderFront:nil];
            [controller.window makeFirstResponder:controller.table];
        }
        printf("NEO98 HOST state-window open rows=%zu\n", lines.size());
        fflush(stdout);
        return true;
    }
}

void Update(const Model &given, const std::string &select)
{
    @autoreleasepool
    {
        if (!controller)
            return;
        model = given;
        if (!select.empty())
            chosen = select;
        [controller reload];
    }
}

bool IsOpen() { return controller && (Screen() ? controller.window.visible : !controller.closing && !lines.empty()); }

void Close()
{
    @autoreleasepool
    {
        if (!controller)
            return;
        controller.closing = YES;
        [controller.window close];
        controller.closing = NO;
        lines.clear();
        if (Screen())
            SDL_ResetKeyboard();
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

bool Perform(const std::string &command)
{
    @autoreleasepool
    {
        const auto equals = command.find('=');
        const std::string name = command.substr(0, equals), argument = equals == std::string::npos ? "" : command.substr(equals + 1);
        if (name == "note" || name == "note-bad")
        {
            // note=PATH|TITLE|DETAIL: a note as the picture of the game shows
            // it, drawn over a grey that stands for the picture.
            const auto one = argument.find('|'), two = one == std::string::npos ? one : argument.find('|', one + 1);
            if (two == std::string::npos)
                return false;
            std::vector<unsigned char> plate;
            int width = 0, height = 0;
            if (!RenderNote(argument.substr(one + 1, two - one - 1), argument.substr(two + 1), name == "note-bad", 2.0, 1100, plate, width, height))
                return false;
            const int margin = 40;
            NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
                                                                            pixelsWide:width + 2 * margin
                                                                            pixelsHigh:height + 2 * margin
                                                                         bitsPerSample:8
                                                                       samplesPerPixel:3
                                                                              hasAlpha:NO
                                                                              isPlanar:NO
                                                                        colorSpaceName:NSDeviceRGBColorSpace
                                                                           bytesPerRow:(width + 2 * margin) * 3
                                                                          bitsPerPixel:24];
            if (!rep)
                return false;
            unsigned char *out = rep.bitmapData;
            for (int y = 0; y < height + 2 * margin; ++y)
                for (int x = 0; x < width + 2 * margin; ++x, out += 3)
                {
                    // Stripes, so the plate is seen to let the picture through.
                    const unsigned char ground = (unsigned char)(((x / 24 + y / 24) & 1) ? 96 : 140);
                    out[0] = ground / 2;
                    out[1] = ground;
                    out[2] = (unsigned char)(ground + 60);
                    const int px = x - margin, py = y - margin;
                    if (px < 0 || py < 0 || px >= width || py >= height)
                        continue;
                    const unsigned char *from = plate.data() + (size_t(py) * width + px) * 4;
                    for (int c = 0; c < 3; ++c)
                        out[c] = (unsigned char)((from[2 - c] * from[3] + out[c] * (255 - from[3])) / 255);
                }
            return [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:Text(argument.substr(0, one)) atomically:YES];
        }
        if (!controller || lines.empty())
            return false;
        if (name == "select")
        {
            for (size_t i = 0; i < lines.size(); ++i)
                if (lines[i].now ? argument == "jetzt" : lines[i].row.id == argument || lines[i].row.name == argument)
                {
                    [controller.table selectRowIndexes:[NSIndexSet indexSetWithIndex:i] byExtendingSelection:NO];
                    [controller.table scrollRowToVisible:NSInteger(i)];
                    chosen = lines[i].now ? std::string() : lines[i].row.id;
                    [controller show];
                    return true;
                }
            return false;
        }
        if (name == "load" || name == "save")
        {
            const Line *line = ChosenLine();
            if (!line || line->now != (name == "save") || !controller.primaryButton.enabled)
                return false;
            if (name == "save")
                controller.nameField.stringValue = Text(argument);
            [controller primary:nil];
            return true;
        }
        if (name == "rename")
        {
            const Line *line = ChosenLine();
            if (!line || line->now || !controller.nameField.enabled)
                return false;
            controller.nameField.stringValue = Text(argument);
            [controller renamed:nil];
            return true;
        }
        if (name == "keep")
        {
            if (controller.keepButton.hidden)
                return false;
            [controller keep:nil];
            return true;
        }
        if (name == "delete")
        {
            if (controller.removeButton.hidden)
                return false;
            [controller remove:nil];
            return true;
        }
        if (name == "close")
        {
            [controller windowWillClose:[NSNotification notificationWithName:NSWindowWillCloseNotification object:nil]];
            return true;
        }
        if (name == "describe")
        {
            for (size_t i = 0; i < lines.size(); ++i)
            {
                const Line &line = lines[i];
                printf("NEO98 HOST state-window row %zu lane=%d top=%d bottom=%d joins=%zu through=%zu id=%s kind=%s usable=%d current=%d chosen=%d name=\"%s\"\n",
                       i, line.lane, line.top ? 1 : 0, line.bottom ? 1 : 0, line.joins.size(), line.through.size(),
                       line.now ? "jetzt" : line.row.id.c_str(), line.now ? "-" : line.row.kind.c_str(), line.now || line.row.usable ? 1 : 0,
                       line.row.current ? 1 : 0, int(i) == ChosenIndex() ? 1 : 0, line.row.name.c_str());
            }
            printf("NEO98 HOST state-window side title=\"%s\" name=\"%s\" when=\"%s\" button=\"%s\" enabled=%d delete=%d keep=%d picture=%d reason=\"%s\"\n",
                   Plain(controller.window.title).c_str(), Plain(controller.nameField.stringValue).c_str(),
                   Plain(controller.whenLabel.stringValue).c_str(), Plain(controller.primaryButton.title).c_str(),
                   controller.primaryButton.enabled ? 1 : 0, controller.removeButton.hidden ? 0 : 1, controller.keepButton.hidden ? 0 : 1,
                   controller.preview.image ? 1 : 0, Plain(controller.reasonLabel.stringValue).c_str());
            printf("NEO98 HOST state-window status \"%s\"\n", Plain(controller.statusLabel.stringValue).c_str());
            fflush(stdout);
            return true;
        }
        if (name == "png" || name == "png-dark")
        {
            // The window drawn into a picture without showing it: its content
            // as AppKit draws it, below a strip with the title (the title bar
            // itself belongs to the window server and is not drawn for a
            // window that is on no screen).
            NSWindow *window = controller.window;
            NSAppearance *look = [NSAppearance appearanceNamed:name == "png" ? NSAppearanceNameAqua : NSAppearanceNameDarkAqua];
            window.appearance = look;
            NSView *view = window.contentView;
            picture_drawing = true;
            [view layoutSubtreeIfNeeded];
            controller.arranging = YES;
            [controller.table reloadData];
            [controller.table selectRowIndexes:[NSIndexSet indexSetWithIndex:NSUInteger(ChosenIndex())] byExtendingSelection:NO];
            controller.arranging = NO;
            [controller.table scrollRowToVisible:ChosenIndex()];
            [view displayIfNeeded];
            NSBitmapImageRep *rep = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
            if (rep)
                [view cacheDisplayInRect:view.bounds toBitmapImageRep:rep];
            picture_drawing = false;
            window.appearance = nil;
            [controller reload];
            if (!rep)
                return false;
            const NSSize size = view.bounds.size;
            const CGFloat bar = 28, factor = rep.pixelsWide / std::max<CGFloat>(1, size.width);
            NSBitmapImageRep *whole = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
                                                                              pixelsWide:rep.pixelsWide
                                                                              pixelsHigh:rep.pixelsHigh + NSInteger(bar * factor)
                                                                           bitsPerSample:8
                                                                         samplesPerPixel:4
                                                                                hasAlpha:YES
                                                                                isPlanar:NO
                                                                          colorSpaceName:NSDeviceRGBColorSpace
                                                                             bytesPerRow:0
                                                                            bitsPerPixel:32];
            NSGraphicsContext *context = whole ? [NSGraphicsContext graphicsContextWithBitmapImageRep:whole] : nil;
            if (!context)
                return false;
            [NSGraphicsContext saveGraphicsState];
            NSGraphicsContext.currentContext = context;
            NSAffineTransform *zoom = [NSAffineTransform transform];
            [zoom scaleBy:factor];
            [zoom concat];
            [look performAsCurrentDrawingAppearance:^{
                [NSColor.windowBackgroundColor setFill];
                NSRectFill(NSMakeRect(0, 0, size.width, size.height + bar));
                [rep drawInRect:NSMakeRect(0, 0, size.width, size.height)];
                NSDictionary *title = @{
                    NSFontAttributeName : [NSFont titleBarFontOfSize:13],
                    NSForegroundColorAttributeName : NSColor.labelColor
                };
                const NSSize text = [window.title sizeWithAttributes:title];
                [window.title drawAtPoint:NSMakePoint((size.width - text.width) / 2, size.height + (bar - text.height) / 2) withAttributes:title];
                [NSColor.separatorColor setFill];
                NSRectFill(NSMakeRect(0, size.height, size.width, 1));
            }];
            [NSGraphicsContext restoreGraphicsState];
            NSData *data = [whole representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
            const bool written = [data writeToFile:Text(argument) atomically:YES];
            printf("NEO98 HOST state-window picture %s %s %ldx%ld\n", argument.c_str(), written ? "written" : "failed", (long)whole.pixelsWide,
                   (long)whole.pixelsHigh);
            fflush(stdout);
            return written;
        }
        return false;
    }
}

bool RenderNote(const std::string &title, const std::string &detail, bool bad, double scale, int max_width, std::vector<unsigned char> &pixels,
                int &width, int &height)
{
    @autoreleasepool
    {
        if (scale < 1)
            scale = 1;
        const CGFloat limit = std::max<CGFloat>(180, std::min<CGFloat>(560, max_width / scale));
        NSMutableParagraphStyle *wrap = [NSMutableParagraphStyle new];
        wrap.lineBreakMode = NSLineBreakByWordWrapping;
        NSDictionary *head = @{
            NSFontAttributeName : [NSFont systemFontOfSize:15 weight:NSFontWeightSemibold],
            NSForegroundColorAttributeName : NSColor.whiteColor,
            NSParagraphStyleAttributeName : wrap
        };
        NSDictionary *body = @{
            NSFontAttributeName : [NSFont systemFontOfSize:12],
            NSForegroundColorAttributeName : [NSColor colorWithWhite:1 alpha:0.86],
            NSParagraphStyleAttributeName : wrap
        };
        const CGFloat pad = 14, mark = 10, gap = 10;
        const CGFloat room = limit - 2 * pad - mark - gap;
        const NSStringDrawingOptions options = NSStringDrawingUsesLineFragmentOrigin | NSStringDrawingUsesFontLeading;
        const NSRect head_box = [Text(title) boundingRectWithSize:NSMakeSize(room, 400) options:options attributes:head];
        const NSRect body_box = detail.empty() ? NSZeroRect : [Text(detail) boundingRectWithSize:NSMakeSize(room, 600) options:options attributes:body];
        const CGFloat text_width = ceil(std::max(head_box.size.width, body_box.size.width));
        const CGFloat head_height = ceil(head_box.size.height), body_height = ceil(body_box.size.height);
        const CGFloat plate_width = text_width + 2 * pad + mark + gap, plate_height = head_height + (detail.empty() ? 0 : body_height + 4) + 2 * pad - 4;
        width = int(ceil(plate_width * scale));
        height = int(ceil(plate_height * scale));
        NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
                                                                        pixelsWide:width
                                                                        pixelsHigh:height
                                                                     bitsPerSample:8
                                                                   samplesPerPixel:4
                                                                          hasAlpha:YES
                                                                          isPlanar:NO
                                                                    colorSpaceName:NSDeviceRGBColorSpace
                                                                       bytesPerRow:width * 4
                                                                      bitsPerPixel:32];
        if (!rep)
            return false;
        NSGraphicsContext *context = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
        if (!context)
            return false;
        [NSGraphicsContext saveGraphicsState];
        NSGraphicsContext.currentContext = context;
        NSAffineTransform *zoom = [NSAffineTransform transform];
        [zoom scaleBy:scale];
        [zoom concat];
        [[NSColor colorWithWhite:0.08 alpha:0.88] setFill];
        [[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(0, 0, plate_width, plate_height) xRadius:10 yRadius:10] fill];
        // The mark: green for what was done, red for what was not.
        [(bad ? [NSColor colorWithRed:1 green:0.32 blue:0.28 alpha:1] : [NSColor colorWithRed:0.3 green:0.85 blue:0.45 alpha:1]) setFill];
        const CGFloat head_y = plate_height - pad + 2 - head_height;
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(pad, head_y + (head_height - mark) / 2 - 1, mark, mark)] fill];
        [Text(title) drawWithRect:NSMakeRect(pad + mark + gap, head_y, room, head_height) options:options attributes:head];
        if (!detail.empty())
            [Text(detail) drawWithRect:NSMakeRect(pad + mark + gap, head_y - 4 - body_height, room, body_height) options:options attributes:body];
        [NSGraphicsContext restoreGraphicsState];
        // RGBA with the colours multiplied by alpha, the top row first: to
        // BGRA with the colours as they are.
        pixels.resize(size_t(width) * height * 4);
        const unsigned char *in = rep.bitmapData;
        const NSInteger stride = rep.bytesPerRow;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                const unsigned char *p = in + y * stride + x * 4;
                unsigned char *q = pixels.data() + (size_t(y) * width + x) * 4;
                const unsigned alpha = p[3];
                q[0] = alpha ? (unsigned char)std::min(255u, p[2] * 255u / alpha) : 0;
                q[1] = alpha ? (unsigned char)std::min(255u, p[1] * 255u / alpha) : 0;
                q[2] = alpha ? (unsigned char)std::min(255u, p[0] * 255u / alpha) : 0;
                q[3] = (unsigned char)alpha;
            }
        return true;
    }
}
} // namespace HostStateWindow
