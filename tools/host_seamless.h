// SPDX-License-Identifier: GPL-2.0-or-later
// The seamless view of the host: the guest's desktop has the size of the
// usable area of the Mac screen, the host window lies over that area without
// a frame, and it is transparent wherever the guest shows its desktop instead
// of a window.
// The shape comes from USER, not from the colours: the visible region of
// every top-level window as USER computes it for a DCE (User::WindowVisibleRects,
// src/user_host_view.inc). The
// guest learns nothing of it but the size of its screen.
// This header is the part without AppKit: the mask, the hit test and the
// picture with its alpha. tools/host_seamless.mm does what needs the NSWindow.
#pragma once
#include "../src/user.h"
#include "../src/win98_frame_control.h"
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

struct SDL_Window;

namespace HostSeamless
{
struct Rect
{
    int left = 0, top = 0, right = 0, bottom = 0;
    bool Holds(int x, int y) const { return x >= left && x < right && y >= top && y < bottom; }
    bool Empty() const { return left >= right || top >= bottom; }
};

// One top-level window of the guest as the host shows it.
struct Window
{
    unsigned handle = 0;
    Rect frame;                // the window rectangle, screen pixels
    unsigned first = 0, count = 0; // its visible rectangles in Mask::rects
    bool shell = false;        // the shell's desktop, shown for its icons
};
// What the view shows besides the windows, and what Collect needs for it.
struct View
{
    bool taskbar = true;       // the taskbar of the shell
    bool icons = false;        // the icons on the shell's desktop
    const unsigned char *pixels = nullptr; // User::Pixels, for first-paint detection
    const unsigned char *icon_mask = nullptr; // original ListView icon/label coverage
    unsigned now = 0;          // milliseconds, for a window that has not painted yet
    unsigned grace = 5000;     // FirstPaintGrace, or what a test wants
    bool trace = false;        // NEO98_SEAMLESS_TRACE: a line when a window is taken as painted
};
// A window that was shown and has not painted: it stays out of the mask
// until it has, or until this time has passed since the host first saw it
// with a place on the screen. A program that loads for a few seconds after
// it showed its window appears when it has drawn, as an application of the
// Mac does; one that never draws appears after this time.
constexpr unsigned FirstPaintGrace = 5000;

// What is opaque on the guest's screen, in screen pixels: the visible regions
// of the top-level windows from front to back, then the edges of the drag
// frame of a move or size without "show window contents while dragging".
struct Mask
{
    int width = 0, height = 0;
    std::vector<Rect> rects;
    std::vector<Window> windows;
    // The minimized windows, front to back: they have no place on the screen
    // (the shell parks them outside), the Mac keeps them in the Dock.
    std::vector<unsigned> minimized;
    // The windows that have a place on the screen and show nothing of it,
    // because other windows cover them wholly, front to back. They have no
    // Mac window; the menu "Window" and the Dock still lead to them.
    std::vector<unsigned> covered;
    // The visible windows the host has seen, and whether one still waits for
    // its first paint (`since` is when the host first saw it).
    struct Seen { unsigned since = 0, shown = 0; bool waiting = false, here = false; };
    std::unordered_map<unsigned, Seen> seen;
    bool Opaque(int x, int y) const
    {
        for (const auto &r : rects)
            if (r.Holds(x, y)) return true;
        return false;
    }
    bool operator==(const Mask &other) const
    {
        return width == other.width && height == other.height && rects.size() == other.rects.size() &&
               (rects.empty() || !memcmp(rects.data(), other.rects.data(), rects.size() * sizeof(Rect)));
    }
};

// Pixel coverage from the original desktop ListView, clipped to USER's visible
// region. Merge equal horizontal runs vertically; wallpaper never enters it.
inline void DesktopIcons(std::vector<Rect> &rects, const Gdi::RECT *visible, unsigned count, const unsigned char *mask,
                         int width, int height)
{
    if (!mask) return;
    for (unsigned k = 0; k < count; ++k) {
        const Rect r = {visible[k].left < 0 ? 0 : visible[k].left, visible[k].top < 0 ? 0 : visible[k].top,
                        visible[k].right > width ? width : visible[k].right,
                        visible[k].bottom > height ? height : visible[k].bottom};
        std::vector<size_t> open, next;
        for (int y = r.top; y < r.bottom; ++y) {
            next.clear();
            int run = -1;
            for (int x = r.left; x <= r.right; ++x) {
                if (x < r.right && mask[size_t(y) * width + x]) { if (run < 0) run = x; continue; }
                if (run < 0) continue;
                bool grown = false;
                for (size_t index : open)
                    if (rects[index].left == run && rects[index].right == x && rects[index].bottom == y) {
                        rects[index].bottom = y + 1; next.push_back(index); grown = true; break;
                    }
                if (!grown) { next.push_back(rects.size()); rects.push_back({run, y, x, y + 1}); }
                run = -1;
            }
            open.swap(next);
        }
    }
}

// More than half of these rectangles of the frame is the colour of the guest's
// desktop (COLOR_BACKGROUND), on a grid of samples: a window that stands
// there has drawn nothing yet.
inline bool StillDesktop(const Gdi::RECT *rects, unsigned count, const unsigned char *pixels, int width, int height)
{
    if (!pixels || !count) return false;
    const unsigned colour = Win98Frame::SystemColors()[1] & 0xffffffu;
    const auto *source = reinterpret_cast<const unsigned *>(pixels);
    unsigned total = 0, same = 0;
    for (unsigned k = 0; k < count; ++k)
    {
        const int left = rects[k].left < 0 ? 0 : rects[k].left, top = rects[k].top < 0 ? 0 : rects[k].top;
        const int right = rects[k].right > width ? width : rects[k].right;
        const int bottom = rects[k].bottom > height ? height : rects[k].bottom;
        for (int y = top + 1; y < bottom; y += 4)
            for (int x = left + 1; x < right; x += 4)
            {
                ++total;
                if ((source[size_t(y) * width + x] & 0xffffffu) == colour) ++same;
            }
    }
    return total && same * 2 > total;
}

// The desktop itself is the desktop window of USER and the window the shell
// registered as its own (SetShellWindow, Explorer's desktop with the icon
// view on it); neither is collected, except the shell's for its icons when
// the view shows them. Without `view.taskbar` the taskbar is left out as
// well, which the host finds the way every Win98 program does, by its class.
// A window that was just shown and has not painted (User::AwaitsPaint, or
// StillDesktop) is left out until it has, for FirstPaintGrace at most: on
// the screen its client still holds the pixels of the desktop.
inline void Collect(Mask &mask, const View &view)
{
    mask.rects.clear();
    mask.windows.clear();
    mask.minimized.clear();
    mask.covered.clear();
    int depth = 32;
    User::ScreenMode(mask.width, mask.height, depth);
    unsigned handles[User::WindowCapacity], count = 0;
    if (!User::WindowSnapshot(0, handles, User::WindowCapacity, count)) return;
    const unsigned shell = User::RegisteredShellWindow();
    const unsigned tray = view.taskbar ? 0 : User::FindWindow("Shell_TrayWnd", nullptr);
    static Gdi::RECT buffer[4096];
    for (auto &entry : mask.seen) entry.second.here = false;
    for (unsigned i = 0; i < count; ++i)
    {
        const unsigned h = handles[i];
        if (h == tray || !User::IsVisible(h)) continue;
        if (h == shell)
        {
            if (!view.icons || !view.icon_mask) continue;
            unsigned n = User::WindowVisibleRects(h, buffer, 4096);
            if (n > 4096) n = 4096;
            Window window;
            window.handle = h;
            window.shell = true;
            window.frame = {0, 0, mask.width, mask.height};
            window.first = unsigned(mask.rects.size());
            DesktopIcons(mask.rects, buffer, n, view.icon_mask, mask.width, mask.height);
            window.count = unsigned(mask.rects.size()) - window.first;
            if (window.count) mask.windows.push_back(window);
            continue;
        }
        if (User::Style(h) & 0x20000000u) { mask.minimized.push_back(h); continue; } // WS_MINIMIZE
        const unsigned shown = User::ShownSerial(h);
        auto found = mask.seen.find(h);
        if (found == mask.seen.end() || found->second.shown != shown)
        {
            Mask::Seen fresh;
            fresh.since = view.now;
            fresh.shown = shown;
            fresh.waiting = true;
            mask.seen[h] = fresh;
            found = mask.seen.find(h);
        }
        Mask::Seen &seen = found->second;
        seen.here = true;
        unsigned n = User::WindowVisibleRects(h, buffer, 4096);
        if (n > 4096) n = 4096;
        // The time runs from the moment the window has a place on the
        // screen: a program may keep a visible window without one for a
        // while and give it its size later.
        if (seen.waiting && !n) seen.since = view.now;
        if (seen.waiting && n)
        {
            // Not painted yet: USER says so, or most of what the window
            // would show is still the desktop's colour. A program may show
            // its window before it invalidates it: the answer of
            // USER at the first sight is not the last word.
            const bool unpainted = User::AwaitsPaint(h) || StillDesktop(buffer, n, view.pixels, mask.width, mask.height);
            if (!unpainted || view.now - seen.since >= view.grace)
            {
                seen.waiting = false;
                if (view.trace)
                    printf("NEO98 HOST seamless first paint hwnd=%x after %u ms awaits=%d desktop=%d rects=%u%s\n", h,
                           view.now - seen.since, User::AwaitsPaint(h) ? 1 : 0,
                           StillDesktop(buffer, n, view.pixels, mask.width, mask.height) ? 1 : 0, n,
                           unpainted ? " (grace over)" : "");
            }
        }
        if (!n)
        {
            Gdi::RECT place{};
            if (User::GetWindowRect(h, &place) && place.right > place.left && place.bottom > place.top && place.right > 0 &&
                place.bottom > 0 && place.left < mask.width && place.top < mask.height)
                mask.covered.push_back(h);
            continue;
        }
        if (seen.waiting) continue;
        Window window;
        window.handle = h;
        Gdi::RECT frame{};
        if (User::GetWindowRect(h, &frame)) window.frame = {frame.left, frame.top, frame.right, frame.bottom};
        window.first = unsigned(mask.rects.size());
        window.count = n;
        for (unsigned k = 0; k < n; ++k)
            mask.rects.push_back({buffer[k].left, buffer[k].top, buffer[k].right, buffer[k].bottom});
        mask.windows.push_back(window);
    }
    // A window that is hidden and shown again waits for its paint anew
    // (User::ShownSerial tells even when the host never saw it hidden).
    for (auto it = mask.seen.begin(); it != mask.seen.end();)
        it = it->second.here ? std::next(it) : mask.seen.erase(it);
    Gdi::RECT outline{};
    int cx = 0, cy = 0;
    if (User::DragOutline(&outline, &cx, &cy))
    {
        const Rect edges[4] = {{outline.left, outline.top, outline.right, outline.top + cy},
                               {outline.left, outline.bottom - cy, outline.right, outline.bottom},
                               {outline.left, outline.top, outline.left + cx, outline.bottom},
                               {outline.right - cx, outline.top, outline.right, outline.bottom}};
        for (auto edge : edges)
        {
            if (edge.left < 0) edge.left = 0;
            if (edge.top < 0) edge.top = 0;
            if (edge.right > mask.width) edge.right = mask.width;
            if (edge.bottom > mask.height) edge.bottom = mask.height;
            if (!edge.Empty()) mask.rects.push_back(edge);
        }
    }
}

// The picture the window shows: `pixels` (User::Pixels, BGRX rows of
// mask.width) with alpha 255 inside the mask and nothing outside. The pointer
// USER laid over the frame may reach beyond a window; where the frame differs
// from the screen surface inside the pointer's box, that is the pointer, and
// it stays. Only a 32-bit desktop has a surface to compare with.
inline void Compose(const Mask &mask, const unsigned char *pixels, std::vector<unsigned> &out)
{
    const int w = mask.width, h = mask.height;
    out.assign(size_t(w) * size_t(h), 0u);
    if (!pixels || w <= 0 || h <= 0) return;
    const auto *source = reinterpret_cast<const unsigned *>(pixels);
    for (auto r : mask.rects)
    {
        if (r.left < 0) r.left = 0;
        if (r.top < 0) r.top = 0;
        if (r.right > w) r.right = w;
        if (r.bottom > h) r.bottom = h;
        for (int y = r.top; y < r.bottom; ++y)
        {
            const unsigned *from = source + size_t(y) * w;
            unsigned *to = out.data() + size_t(y) * w;
            for (int x = r.left; x < r.right; ++x) to[x] = from[x] | 0xff000000u;
        }
    }
    Gdi::RECT box{};
    int sw = 0, sh = 0, depth = 0;
    User::ScreenMode(sw, sh, depth);
    const unsigned char *screen = User::ScreenBits();
    if (!User::PointerDrawn(&box) || !screen || depth != 32 || sw != w || sh != h) return;
    const int stride = User::ScreenStride();
    for (int y = box.top; y < box.bottom && y < h; ++y)
    {
        const unsigned *from = source + size_t(y) * w;
        const auto *under = reinterpret_cast<const unsigned *>(screen + size_t(y) * stride);
        unsigned *to = out.data() + size_t(y) * w;
        for (int x = box.left; x < box.right && x < w; ++x)
            if (!to[x] && ((from[x] ^ under[x]) & 0xffffffu)) to[x] = from[x] | 0xff000000u;
    }
}

// The alpha of a composed picture as a PGM, for the tests.
inline bool WriteAlpha(const char *path, const std::vector<unsigned> &picture, int width, int height)
{
    FILE *file = fopen(path, "wb");
    if (!file) return false;
    fprintf(file, "P5\n%d %d\n255\n", width, height);
    std::vector<unsigned char> row(size_t(width > 0 ? width : 0));
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x) row[size_t(x)] = (unsigned char)(picture[size_t(y) * width + x] >> 24);
        fwrite(row.data(), 1, row.size(), file);
    }
    return fclose(file) == 0;
}

// --- One Mac window per guest window ---------------------------------------
// The second form of the seamless view: every visible top-level window of the
// guest gets a borderless NSWindow of its own that shows its part of the
// frame, so Mac windows can lie between two guest windows. An Item is one such
// NSWindow; Plan lists them from front to back.
constexpr unsigned PointerItem = 0xfffffff2u, OutlineItem = 0xfffffff1u;
struct Item
{
    unsigned id = 0;           // the HWND, or PointerItem, or OutlineItem
    Rect frame;                // screen pixels; window bounds keep their full size
    std::vector<Rect> rects;   // what is opaque in it, screen pixels
    std::string title;
    bool listed = false;       // a window of its own in Mission Control and the window menu
    bool through = false;      // never takes the mouse
    bool pointer = false;      // shows the pointer: the frame where it differs from the screen surface
    bool desktop = false;      // the icons of the shell's desktop: behind the Mac's windows
    bool minimized = false;    // minimized in the guest: its Mac window is in the Dock, the frame is empty
};
// The guest's text is Windows-1252; the Mac wants UTF-8.
inline std::string WindowTitle(unsigned handle)
{
    char text[256] = {};
    User::GetWindowText(handle, text, sizeof text);
    std::string title;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p; ++p)
    {
        if (*p < 0x80) title += char(*p);
        else { title += char(0xc0 | (*p >> 6)); title += char(0x80 | (*p & 0x3f)); }
    }
    return title;
}
// A window of the guest that would have a button on the Win98 taskbar.
inline bool TaskbarWindow(unsigned handle)
{
    const unsigned extended = User::ExtendedStyle(handle);
    return (extended & 0x40000u) || (!User::Owner(handle) && !(extended & 0x80u));
}
inline Rect Clipped(Rect r, int width, int height)
{
    if (r.left < 0) r.left = 0;
    if (r.top < 0) r.top = 0;
    if (r.right > width) r.right = width;
    if (r.bottom > height) r.bottom = height;
    return r;
}
// A guest window gets an entry of its own on the Mac when it would have a
// button on the Win98 taskbar: no owner and no WS_EX_TOOLWINDOW, or
// WS_EX_APPWINDOW. Menus, tooltips, owned dialogs and the taskbar itself
// belong to the application without one.
inline void Plan(const Mask &mask, std::vector<Item> &items)
{
    items.clear();
    Gdi::RECT box{};
    if (User::PointerDrawn(&box))
    {
        Item item;
        item.id = PointerItem;
        item.frame = Clipped({box.left, box.top, box.right, box.bottom}, mask.width, mask.height);
        item.through = item.pointer = true;
        if (!item.frame.Empty()) items.push_back(item);
    }
    const size_t in_windows = mask.windows.empty() ? 0 : mask.windows.back().first + mask.windows.back().count;
    if (mask.rects.size() > in_windows)
    {
        // The edges of the drag frame, after the windows in Mask::rects.
        Item item;
        item.id = OutlineItem;
        item.through = true;
        item.frame = mask.rects[in_windows];
        for (size_t i = in_windows; i < mask.rects.size(); ++i)
        {
            const Rect &r = mask.rects[i];
            item.rects.push_back(r);
            if (r.left < item.frame.left) item.frame.left = r.left;
            if (r.top < item.frame.top) item.frame.top = r.top;
            if (r.right > item.frame.right) item.frame.right = r.right;
            if (r.bottom > item.frame.bottom) item.frame.bottom = r.bottom;
        }
        items.push_back(item);
    }
    for (const auto &window : mask.windows)
    {
        Item item;
        item.id = window.handle;
        item.frame = window.frame;
        if (item.frame.Empty()) continue;
        item.rects.assign(mask.rects.begin() + window.first, mask.rects.begin() + window.first + window.count);
        item.title = WindowTitle(window.handle);
        item.listed = TaskbarWindow(window.handle);
        if (item.title.empty() || window.shell) item.listed = false;
        item.desktop = window.shell;
        items.push_back(item);
    }
    // A minimized window keeps its Mac window, which goes into the Dock
    // (Present); one click there brings it back.
    for (const unsigned handle : mask.minimized)
    {
        Item item;
        item.id = handle;
        item.minimized = item.through = true;
        item.title = WindowTitle(handle);
        item.listed = TaskbarWindow(handle) && !item.title.empty();
        if (item.listed) items.push_back(item);
    }
}
// The picture of one item: its frame out of `pixels` (rows of `width`), alpha
// 255 inside its rectangles. The pointer item shows the frame where it
// differs from the screen surface, as in Compose.
inline void ComposeItem(const Item &item, const unsigned char *pixels, int width, int height, std::vector<unsigned> &out)
{
    const int w = item.frame.right - item.frame.left, h = item.frame.bottom - item.frame.top;
    out.assign(size_t(w > 0 ? w : 0) * size_t(h > 0 ? h : 0), 0u);
    if (!pixels || w <= 0 || h <= 0) return;
    const auto *source = reinterpret_cast<const unsigned *>(pixels);
    if (item.pointer)
    {
        int sw = 0, sh = 0, depth = 0;
        User::ScreenMode(sw, sh, depth);
        const unsigned char *screen = User::ScreenBits();
        if (!screen || depth != 32 || sw != width || sh != height) return;
        const int stride = User::ScreenStride();
        for (int y = item.frame.top; y < item.frame.bottom; ++y)
        {
            const unsigned *from = source + size_t(y) * width;
            const auto *under = reinterpret_cast<const unsigned *>(screen + size_t(y) * stride);
            unsigned *to = out.data() + size_t(y - item.frame.top) * w;
            for (int x = item.frame.left; x < item.frame.right; ++x)
                if ((from[x] ^ under[x]) & 0xffffffu) to[x - item.frame.left] = from[x] | 0xff000000u;
        }
        return;
    }
    for (const auto &rect : item.rects)
    {
        Rect r = rect;
        if (r.left < 0) r.left = 0;
        if (r.top < 0) r.top = 0;
        if (r.left < item.frame.left) r.left = item.frame.left;
        if (r.top < item.frame.top) r.top = item.frame.top;
        if (r.right > item.frame.right) r.right = item.frame.right;
        if (r.bottom > item.frame.bottom) r.bottom = item.frame.bottom;
        if (r.right > width) r.right = width;
        if (r.bottom > height) r.bottom = height;
        for (int y = r.top; y < r.bottom; ++y)
        {
            const unsigned *from = source + size_t(y) * width;
            unsigned *to = out.data() + size_t(y - item.frame.top) * w;
            for (int x = r.left; x < r.right; ++x) to[x - item.frame.left] = from[x] | 0xff000000u;
        }
    }
}
// How much of what an item shows is still the colour of the guest's desktop
// (COLOR_BACKGROUND), in percent of a grid of samples: a window that is on
// the screen before it has drawn anything shows as a box in that colour.
inline int DesktopShare(const Item &item, const unsigned char *pixels, int width, int height)
{
    if (!pixels) return 0;
    const unsigned colour = Win98Frame::SystemColors()[1] & 0xffffffu;
    const auto *source = reinterpret_cast<const unsigned *>(pixels);
    unsigned total = 0, same = 0;
    for (const auto &rect : item.rects)
    {
        const Rect r = Clipped(rect, width, height);
        for (int y = r.top + 1; y < r.bottom; y += 4)
            for (int x = r.left + 1; x < r.right; x += 4)
            {
                ++total;
                if ((source[size_t(y) * width + x] & 0xffffffu) == colour) ++same;
            }
    }
    return total ? int(same * 100 / total) : 0;
}
// What the Mac's mouse did in one of the item windows, in screen pixels of
// the guest; `chosen` is a window picked in Mission Control or the window
// menu.
struct MouseEvent
{
    // Restore: the user took the window out of the Dock again.
    enum Kind { Move, Button, Wheel, Leave, Chosen, Restore } kind = Move;
    // Wheel: units of WHEEL_DELTA up and to the right, flags of HostScrollFine.
    int x = 0, y = 0, wheel = 0, wheel_x = 0;
    unsigned wheel_flags = 0;
    unsigned buttons = 0, window = 0;
};

// --- The pointer --------------------------------------------------------------
// In the seamless view the pointer is the Mac's: USER draws none into the
// frame, and the host sets the cursor of the Mac after the cursor the guest
// has set (X86::Application::HostPointer). A cursor of USER's own (IDC_ARROW
// and the rest, known by its handle, not by its picture) becomes its Mac
// counterpart; a program's own cursor becomes a Mac cursor with the guest's
// picture and hot spot.
struct Cursor
{
    bool shown = false;          // the guest shows a pointer at all
    unsigned identifier = 0;     // the OEM identifier, zero for a program's own
    unsigned handle = 0;
    int width = 0, height = 0, hot_x = 0, hot_y = 0;
    std::vector<unsigned> argb;  // rows of width, alpha 255 where it covers
    bool operator==(const Cursor &o) const
    {
        return shown == o.shown && identifier == o.identifier && handle == o.handle && width == o.width &&
               height == o.height && hot_x == o.hot_x && hot_y == o.hot_y && argb == o.argb;
    }
};
// What the Mac shows for it; "custom" is the guest's own picture.
inline const char *CursorName(const Cursor &cursor)
{
    if (!cursor.shown) return "hidden";
    switch (cursor.identifier)
    {
    case 32512: return "arrow";
    case 32513: return "ibeam";
    case 32514: return "wait";
    case 32515: return "cross";
    case 32516: return "uparrow";
    case 32642: return "size-nwse";
    case 32643: return "size-nesw";
    case 32644: return "size-we";
    case 32645: return "size-ns";
    case 32646: return "size-all";
    case 32648: return "no";
    case 32649: return "hand";
    case 32650: return "appstarting";
    case 32651: return "help";
    default: return cursor.width > 0 ? "custom" : "arrow";
    }
}

// --- tools/host_seamless.mm ---------------------------------------------------
// The cursor of the Mac for the guest's pointer, over the guest's windows;
// `over_guest` false gives the arrow back (the layer form calls it as the
// pointer crosses the mask).
void SetCursor(const Cursor &cursor);
void ApplyCursor(bool over_guest);
// The NSWindows for the items: made, moved, filled, ordered like the guest
// orders its windows and closed again. `anchor` is the window of SDL lying
// over the usable area; it stays there, empty and click-through, and keeps
// the keyboard.
void Present(SDL_Window *anchor, const std::vector<Item> &items, const unsigned char *pixels, int width, int height);
void CloseItems();
bool NextMouse(MouseEvent &event);
// Watches the scrollWheel events of the application for the SDL window:
// HostScrollSeen of tools/host_wheel.h.
void WatchScroll();
// How many item windows exist, for the log.
unsigned ItemWindows();
// A test's mouse at a point of the guest's screen: a press or release of the
// left button, a move (a drag while the button is down) or one notch of the
// wheel, handed to the window the window server has there, if it is an item
// window that takes the mouse; answers its guest window. The real pointer
// stays where it is.
enum class TestMouse { Up, Down, Move, WheelUp, WheelDown };
unsigned PostMouse(int x, int y, TestMouse what);
// All item windows in front of the Mac's windows, in the guest's order.
void RaiseItems(const std::vector<Item> &items);
// The window of SDL as an overlay: without a shadow, not opaque, its layer
// with an alpha channel; and back. False where there is no NSWindow (SDL's
// dummy driver), which changes nothing for the caller.
bool Transparent(SDL_Window *window, bool on);
// The native window with its views and layers, one line each: what decides
// whether the desktop shows through. For runs in a real window.
std::string Describe(SDL_Window *window);
// Before each picture of the overlay: SDL's own black behind it goes away.
void KeepClear(SDL_Window *window);
// Mouse clicks go through the whole window to whatever lies below (NSWindow
// ignoresMouseEvents). The host switches it as the pointer crosses the mask.
void ClickThrough(SDL_Window *window, bool on);
// The pointer in points of the window's content, wherever it is on the
// screen and whoever owns the click. False without a window server.
bool Pointer(SDL_Window *window, int &x, int &y);
// The application is the active one: the keyboard is the guest's.
bool Active();
} // namespace HostSeamless
