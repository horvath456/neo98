// SPDX-License-Identifier: GPL-2.0-or-later
// Included inside USER's native window provider. The nonclient hit test, the
// modal move/size loop and the nonclient scrollbar tracker. All three run the
// way the menu tracker does: a native state machine, a nested message pump on
// the guest fiber, and first refusal on every input packet.
namespace {
void *frame_context = nullptr;
FrameSend frame_send = nullptr;
FrameRect frame_rect = nullptr;
FrameLoop frame_loop = nullptr;
FrameCommit frame_commit = nullptr;

// The session both trackers share. USER runs one at a time, held by capture.
enum TrackKind { TrackNone = 0, TrackMove = 1, TrackSize = 2, TrackScroll = 3 };
struct TrackSession
{
    unsigned kind = TrackNone;
    bool running = false, finished = false, cancelled = false;
    HWND window = 0;
    // Move and size.
    unsigned edge = 0;            // WMSZ_, zero while moving
    Gdi::RECT origin = {}, rect = {};
    int anchor_x = 0, anchor_y = 0;
    bool keyboard = false, moved = false;
    int min_width = 0, min_height = 0, max_width = 0, max_height = 0;
    // Scrollbar. `part` is USER's word at data 0E2Ah, `zone` the drag tolerance
    // rectangle for a thumb and the pressed region for everything else.
    unsigned vertical = 0, part = Win98ScrollTrack::None;
    bool control = false;          // SB_CTL: the bar is the window itself
    unsigned start_position = 0, track_position = 0;
    int thumb_grab = 0, thumb_pixel = 0, start_pixel = 0;
    Win98ScrollTrack::Rect zone = {};
    unsigned next_repeat = 0;
    bool inside = true;
    // Outline dragging when full-window dragging is disabled. `outline` is
    // the inverted screen frame, `clip` its clipping rectangle, and `brush`
    // the pattern origin at the old window corner.
    bool outlined = false, locked = false;
    Win98::Rect outline = {};
    Gdi::RECT clip = {};
    int thickness = 1, brush_x = 0, brush_y = 0;
    Gdi::HBRUSH gray = 0;
} track;
// USER's tracking record, the block its data word 10DEh points at. It lives
// from SBTrackInit to the end of SBTrackEnd, so the SB_THUMBPOSITION and
// SB_ENDSCROLL the tracker sends on the way out still answer the tracked
// position. GetScrollInfo reads the window at +0x18, the bar at +0x1c and the
// position at data 0E2Ch (USER).
struct ScrollRecord
{
    bool live = false;
    HWND window = 0;
    unsigned bar = 0, position = 0;
} scroll_record;

unsigned TrackSend(HWND h, unsigned message, unsigned wp, unsigned lp)
{
    if (!frame_send || !IsWindow(h)) return 0;
    return frame_send(CallbackContext(frame_context, h), h, message, wp, lp);
}
bool TrackRectMessage(HWND h, unsigned message, unsigned wp, Gdi::RECT &r)
{
    if (!frame_rect || !IsWindow(h)) return false;
    return frame_rect(CallbackContext(frame_context, h), h, message, wp, &r) != 0;
}
// SPI 26h reads the switch at DS:234h only with the Plus module at 264h.
bool FullDrag() { return settings.Word(0x264) && settings.Word(0x234); }
// An 8x8 checkerboard brush. PATINVERT flips alternating pixels and preserves
// the others.
Gdi::HBRUSH GrayBrush()
{
    Gdi::BITMAPINFO info = {{40, 8, -8, 1, 32, 0, 0, 0, 0, 0, 0}};
    void *bits = nullptr;
    const auto bitmap = Gdi::CreateDIBSection(screen, &info, 0, &bits);
    if (!bitmap || !bits) { if (bitmap) Gdi::DeleteObject(bitmap); return 0; }
    for (unsigned y = 0; y < 8; ++y)
        for (unsigned x = 0; x < 8; ++x)
            static_cast<unsigned *>(bits)[y * 8 + x] = ((x ^ y) & 1) ? 0 : 0xffffff;
    const auto brush = Gdi::CreatePatternBrush(bitmap);
    Gdi::DeleteObject(bitmap);
    return brush;
}
// Use a desktop DC with
// DCX_WINDOW|DCX_CACHE|DCX_INTERSECTRGN|DCX_LOCKWINDOWUPDATE. It can draw
// despite the lock, clipped to the drag rectangle, with brush origin at the
// previous window corner.
template <class Draw>
void OutlineDC(Draw draw)
{
    if (!screen || !track.gray) return;
    const int saved = Gdi::SaveDC(screen);
    if (!saved) return;
    Gdi::ResetSurfaceDC(screen);
    Gdi::SetDeviceOrigin(screen, 0, 0);
    if (Gdi::IntersectClipRegion(screen, &track.clip, 1)) {
        Gdi::SetBrushOrgEx(screen, track.brush_x, track.brush_y);
        const auto previous = Gdi::SelectObject(screen, track.gray);
        draw([](int x, int y, int width, int height) {
            if (width > 0 && height > 0) Gdi::PatBlt(screen, x, y, width, height, 0x5a0049);
        });
        if (previous) Gdi::SelectObject(screen, previous);
    }
    Gdi::RestoreDC(screen, saved);
    DrawingChanged(screen);
}
// Invert the full patterned frame to display it initially or erase it later.
void OutlineFrame()
{
    OutlineDC([](auto blt) {
        Win98DragRect::DrawFrame(track.outline, track.thickness, settings.metrics[5], settings.metrics[6], blt);
    });
}
// DrawDragRect moves the outline only when its rectangle changes.
void OutlineMove(const Gdi::RECT &r)
{
    const Win98::Rect now{r.left, r.top, r.right, r.bottom};
    if (!track.outlined || Win98::EqualRect(&track.outline, &now)) return;
    OutlineDC([&](auto blt) {
        Win98DragRect::DrawDragRect(track.outline, now, track.thickness, settings.metrics[5], settings.metrics[6], blt);
    });
}
// Constrain child dragging to the parent's client area intersected with its
// window-region bounds. Top-level windows use the monitor work area, except
// topmost/tool windows or multiple-monitor configurations, which use the
// screen area. This provider exposes one monitor.
Gdi::RECT DragLimit(const Window &w)
{
    if (const auto *parent = w.parent ? Find(w.parent) : nullptr) {
        Gdi::RECT r = ClientScreenRect(*parent);
        if (parent->region) {
            auto &p = const_cast<Window &>(*parent);
            Gdi::RECT box{}, cut{};
            if (SyncWindowRegion(p) && Gdi::RegionBounds(p.region, &box))
                r = Win98::IntersectRect(&cut, &r, &box) ? cut : Gdi::RECT{};
        }
        return r;
    }
    Gdi::RECT r{};
    if (!(w.extended & 0x88u) && WorkArea(&r)) return r;
    int width = 0, height = 0, depth = 0;
    ScreenMode(width, height, depth);
    return {0, 0, width, height};
}
// ClipCursor confines the pointer during dragging; ClipCursor(NULL) releases
// it. Update the pointer position without sending a mouse message.
void ClipDragPointer(const Gdi::RECT *r)
{
    int x = -1, y = -1;
    ClipPointer(r, &x, &y);
    if (x < 0) return;
    mouse_x = x; mouse_y = y;
    MovePointer();
}
// Outline dragging draws the frame and locks the desktop with
// LockWindowUpdate. Other DCs cannot overwrite the outline until the drag
// ends.
void OutlineBegin(HWND h, const Window &w)
{
    track.outline = {track.rect.left, track.rect.top, track.rect.right, track.rect.bottom};
    // Non-minimized WS_THICKFRAME windows use SPI_GETBORDER plus three
    // pixels; other windows use one pixel.
    track.thickness = (w.style & 0x00040000u) && !(w.style & 0x20000000u) ? settings.Word(0x1014) + 3 : 1;
    track.brush_x = track.rect.left; track.brush_y = track.rect.top;
    // Paint pending frame and caption changes before taking the lock,
    // matching immediate nonclient activation painting.
    Render();
    track.gray = GrayBrush();
    if (!track.gray) return;
    track.outlined = true;
    OutlineFrame();
    if (!locked_window) {
        locked_window = Desktop; locked_process = CurrentProcess(); locked_thread = CurrentThread();
        LockBounds(true);
        track.locked = true;
        GeometryChanged();
    }
    (void)h;
}
// Erase the outline.
void OutlineErase()
{
    if (track.outlined) OutlineFrame();
    track.outlined = false;
    if (track.gray) Gdi::DeleteObject(track.gray);
    track.gray = 0;
}
// Unlock and invalidate areas that attempted to draw while locked.
void OutlineUnlock()
{
    if (track.locked && locked_window == Desktop) {
        DropWindowUpdateLock(true);
        GeometryChanged();
    }
    track.locked = false;
}
} // namespace

// DefWindowProc WM_NCHITTEST on the native record.
unsigned FrameHitTest(HWND h, int x, int y)
{
    auto *w = Find(h);
    if (!w) return Win98FrameHit::Nowhere;
    Win98FrameHit::Window record;
    HitWindowRecord(h, *w, record);
    const unsigned frame = Win98FrameHit::Test(HitMetrics(), record, x, y);
    // STATIC answers HTTRANSPARENT unless it is SS_NOTIFY, a group box always.
    if (w->label || w->button)
        return Win98WindowHit::ControlHit(w->label ? 3 : 2, w->style, frame);
    return frame;
}
// The window's own answer. Explorer and many applications override WM_NCHITTEST,
// so the native shortcut has to ask the procedure before it acts on a press.
unsigned HitTest(HWND h, int x, int y)
{
    auto *w = Find(h);
    if (!w) return Win98FrameHit::Nowhere;
    const bool guest = w->procedure && (w->procedure < 0x71000000u || w->procedure >= 0x71010000u);
    if (guest && frame_send)
    {
        const unsigned answer = TrackSend(h, 0x84, 0,
                                          (unsigned short)x | ((unsigned)(unsigned short)y << 16));
        if (!IsWindow(h)) return Win98FrameHit::Nowhere;
        return (unsigned short)answer == 0xffff ? Win98FrameHit::Transparent : (unsigned short)answer;
    }
    return FrameHitTest(h, x, y);
}
namespace {
// The bar geometry the tracker works on, in screen coordinates along the bar's
// own axes.
bool TrackGeometry(HWND h, unsigned vertical, Win98Scroll::Geometry &g, Gdi::RECT &rect)
{
    auto *w = Find(h);
    if (!w) return false;
    if (w->common == 10) { unsigned axis = 0; return ScrollControlGeometry(h, *w, rect, g, axis); }
    return ScrollGeometry(h, *w, vertical, Origin(*w), rect, g);
}
unsigned TrackDisabled(HWND h, unsigned vertical)
{
    auto *w = Find(h);
    return w ? (w->scroll.disabled >> (vertical * 2)) & 3u : 3u;
}
Win98Scroll::Bar TrackBar(HWND h, unsigned vertical)
{
    auto *w = Find(h);
    return w ? w->scroll.bars[vertical] : Win98Scroll::Bar{};
}
// WM_HSCROLL/WM_VSCROLL for a nonclient bar: lParam is zero, the bar is the
// window's own. USER sends these, it does not post them.
// USER. The message goes to the owner: the window itself for a
// nonclient bar, the parent for a SCROLLBAR control, whose handle then travels
// in lParam.
void TrackScrollMessage(unsigned code, unsigned position)
{
    if (!IsWindow(track.window)) return;
    const HWND owner = track.control ? Parent(track.window) : track.window;
    if (!owner) return;
    TrackSend(owner, track.vertical ? 0x115u : 0x114u, code | (position << 16),
              track.control ? track.window : 0);
}
// USER. The pressed part is inverted (pages) or drawn pushed (arrows)
// while the pointer is on it, and restored the moment it is not.
void TrackPress(bool pressed)
{
    namespace S = Win98ScrollTrack;
    if (track.part == S::LineUp || track.part == S::LineDown)
    {
        arrow_window = pressed ? track.window : 0;
        arrow_vertical = track.vertical;
        arrow_part = pressed ? track.part : S::None;
    }
    else if (track.part == S::PageUp || track.part == S::PageDown)
    {
        if (pressed)
        {
            scroll_press.part = track.part;
            scroll_press.window = track.window;
            scroll_press.vertical = track.vertical;
            scroll_press.left = track.zone.left; scroll_press.top = track.zone.top;
            scroll_press.right = track.zone.right; scroll_press.bottom = track.zone.bottom;
        }
        else scroll_press = Win98Scroll::Press{};
    }
    dirty = true;
}
void TrackStep()
{
    if (track.part == Win98ScrollTrack::Thumb) return;
    TrackScrollMessage(track.part, 0);
}
void EndTrack(bool cancel)
{
    if (!track.kind) return;
    const unsigned kind = track.kind;
    const HWND window = track.window;
    const unsigned edge = track.edge;
    const Gdi::RECT rect = track.rect;
    const bool moved = track.moved;
    track.cancelled = cancel;
    track.finished = true;
    track.kind = TrackNone;
    const unsigned part = track.part;
    // USER: the pressed region is restored on the way out, then the
    // window's press bits are cleared. Clear these bits before returning.
    if (kind == TrackScroll && track.inside) TrackPress(false);
    scroll_press = Win98Scroll::Press{};
    arrow_window = 0; arrow_part = Win98ScrollTrack::None;
    scroll_thumb_active = false;
    // Erase the outline, release pointer clipping, then unlock the screen.
    if (kind != TrackScroll) { OutlineErase(); ClipDragPointer(nullptr); OutlineUnlock(); }
    // The release runs where the input is routed; the capture is in the
    // queue of the tracked window.
    CaptureOperation(9, window);
    dirty = true;
    if (kind == TrackScroll)
    {
        // USER. A cancel reports the record's own position again.
        if (part == Win98ScrollTrack::Thumb)
        {
            const unsigned reported = cancel ? TrackBar(window, track.vertical).position
                                             : track.track_position;
            scroll_record.position = reported;
            TrackScrollMessage(Win98ScrollTrack::SbThumbPosition, reported);
        }
        TrackScrollMessage(Win98ScrollTrack::SbEndScroll, 0);
        // SBTrackEnd drops the record last, so both messages above still find
        // the tracked position.
        scroll_record = ScrollRecord{};
        return;
    }
    if (!IsWindow(window)) return;
    if (!cancel && moved && frame_commit)
        frame_commit(CallbackContext(frame_context, window), window, rect.left, rect.top,
                     rect.right - rect.left, rect.bottom - rect.top, edge ? 0x14u : 0x15u);
    if (IsWindow(window)) TrackSend(window, 0x232, 0, 0); // WM_EXITSIZEMOVE
    (void)edge;
}
// The drag rectangle after a pointer move, with the guest's WM_SIZING/WM_MOVING
// answer applied.
void TrackTo(int x, int y)
{
    Gdi::RECT proposed;
    Win98SizeMove::Rect r{track.origin.left, track.origin.top, track.origin.right, track.origin.bottom};
    Win98SizeMove::Rect out{};
    Win98SizeMove::Apply(out, r, track.edge, x - track.anchor_x, y - track.anchor_y);
    Win98SizeMove::Limit(out, track.edge, track.min_width, track.min_height,
                         track.max_width, track.max_height);
    proposed = {out.left, out.top, out.right, out.bottom};
    // WM_SIZING and WM_MOVING carry a rectangle the window may change.
    if (track.edge) TrackRectMessage(track.window, 0x214, track.edge, proposed);
    else TrackRectMessage(track.window, 0x216, 0, proposed);
    if (!IsWindow(track.window)) { EndTrack(true); return; }
    if (proposed.left != track.rect.left || proposed.top != track.rect.top ||
        proposed.right != track.rect.right || proposed.bottom != track.rect.bottom)
    {
        track.rect = proposed;
        track.moved = true;
        // "Show window contents while dragging" moves the window itself;
        // without it the inverted outline follows.
        if (!FullDrag()) OutlineMove(track.rect);
        else {
            dirty = true;
            if (frame_commit)
                frame_commit(CallbackContext(frame_context, track.window), track.window,
                             track.rect.left, track.rect.top, track.rect.right - track.rect.left,
                             track.rect.bottom - track.rect.top, track.edge ? 0x14u : 0x15u);
        }
    }
}
// The pointer moved over a tracked scrollbar.
void TrackScrollTo(int x, int y)
{
    Win98Scroll::Geometry g;
    Gdi::RECT rect;
    if (!TrackGeometry(track.window, track.vertical, g, rect)) return;
    namespace S = Win98ScrollTrack;
    const int along = track.vertical ? y : x;
    if (track.part == S::Thumb)
    {
        // USER. Outside the tolerance zone the thumb snaps back to the
        // pixel the drag started at and keeps reporting that position.
        const int pixel = S::In(track.zone, x, y) ? along + track.thumb_grab : track.start_pixel;
        if (pixel == track.thumb_pixel) return;   //
        track.thumb_pixel = pixel;
        const unsigned position = S::Position(TrackBar(track.window, track.vertical), g, pixel);
        scroll_thumb_active = true;
        scroll_thumb_window = track.window;
        scroll_thumb_vertical = track.vertical;
        scroll_thumb_position = position;
        dirty = true;
        if (position == track.track_position) return;   //
        track.track_position = position;
        scroll_record.position = position;
        TrackScrollMessage(S::SbThumbTrack, position);
        return;
    }
    // USER. The stored region shrinks against the live thumb, so a page
    // repeat stops by itself once the thumb has reached the pointer.
    S::ClipPage(track.zone, g, track.part, track.vertical);
    const bool inside = S::In(track.zone, x, y);
    if (inside != track.inside)
    {
        track.inside = inside;
        TrackPress(inside);
        // Re-entering re-arms with the repeat interval, not the first delay.
        if (inside) { track.next_repeat = MessageClock() + S::RepeatDelay(double_click_time); TrackStep(); }
    }
}
// USER, the SIF_TRACKPOS branch of GetScrollInfo. The original reaches
// the tracking record through its data word 10DEh and answers the live word at
// 0E2Ch only when the record's window at +0x18 and its bar at +0x1c are the
// ones the caller asks about. Declared above the scroll family, which is
// included before this tracker.
bool ScrollTrackPosition(HWND h, unsigned bar, unsigned &position)
{
    if (!scroll_record.live || scroll_record.window != h || scroll_record.bar != bar)
        return false;
    position = scroll_record.position;
    return true;
}
} // namespace

bool FrameTrackActive() { return track.kind != TrackNone; }
// USER reads the "in move/size loop" flag at its data 1022h and the
// window at 756h. GetGUIThreadInfo reports both as GUI_INMOVESIZE.
HWND SizeMoveWindow() { return track.kind == TrackMove || track.kind == TrackSize ? track.window : 0; }
// USER arms a system timer (id -2) for the arrow and page repeat. The native
// scheduler has no timer list of its own for this, so a waiting tracker keeps
// its thread runnable while its repeat is due.
bool FrameTrackDue()
{
    if (track.kind != TrackScroll || track.part == Win98ScrollTrack::Thumb) return false;
    if (!track.inside || !track.next_repeat) return false;
    const unsigned now = MessageClock();
    return now && (int)(now - track.next_repeat) >= 0;
}
void CancelFrameTrack() { if (track.kind) EndTrack(true); }
// USER ends a scrollbar track, the menu and the capture only when the
// window that gets WM_CANCELMODE is the one holding them; the move/size loop
// of another window goes on. A program that disables a button of
// another window while the user drags (EnableWindow sends WM_CANCELMODE,
// USER) no longer drops the drag.
void CancelFrameTrack(HWND h) { if (track.kind && track.window == h) EndTrack(true); }
namespace {
// USER warps the pointer for the keyboard variants. The move re-enters the
// router, which advances the drag with the new position.
void WarpPointer(int x, int y)
{
    const int width = settings.metrics[0], height = settings.metrics[1];
    if (x < 0) x = 0; if (x > width - 1) x = width - 1;
    if (y < 0) y = 0; if (y > height - 1) y = height - 1;
    MouseButtons(x, y, PhysicalMouseButtons());
}
}

// The repeat timer of a held arrow or a held page region, and the modal loop's
// chance to notice that its window went away.
void FrameTrackTick()
{
    if (track.kind == TrackNone) return;
    if (!IsWindow(track.window)) { EndTrack(true); return; }
    // No look at the physical button: the loop of USER ends only on
    // the release it retrieves itself (messages 0200h..020Ah, capture set
    // ). A release queued right behind the press is routed only
    // after the press was read, so it reaches the tracker as input
    // and not the window as WM_NCLBUTTONUP.
    if (track.kind != TrackScroll || track.part == Win98ScrollTrack::Thumb) return;
    if (!track.inside || !track.next_repeat) return;
    const unsigned now = MessageClock();
    if (!now || (int)(now - track.next_repeat) < 0) return;
    // USER re-arms the one system timer (id -2) with the repeat delay.
    track.next_repeat = now + Win98ScrollTrack::RepeatDelay(double_click_time);
    TrackStep();
}
// Messages the modal pump retrieves. Escape cancels a move or size drag; the
// tracker keeps everything else moving to its window.
bool FrameTrackMessage(const Message &m)
{
    if (track.kind == TrackNone) return false;
    if (m.message == 0x100 || m.message == 0x104)
    {
        if (m.wparam == 0x1b) { EndTrack(true); return true; }
        if (m.wparam == 0x0d) { EndTrack(false); return true; }
        if (track.kind != TrackScroll && track.keyboard &&
            m.wparam >= 0x25 && m.wparam <= 0x28)
        {
            if (track.kind == TrackSize && !track.edge)
            {
                track.edge = Win98SizeMove::EdgeFromKey(0, m.wparam);
                Win98SizeMove::Rect r{track.rect.left, track.rect.top, track.rect.right, track.rect.bottom};
                Win98SizeMove::KeyboardAnchor(r, track.edge, track.anchor_x, track.anchor_y);
                WarpPointer(track.anchor_x, track.anchor_y);
                return true;
            }
            const int step = 8;
            const int dx = m.wparam == 0x25 ? -step : m.wparam == 0x27 ? step : 0;
            const int dy = m.wparam == 0x26 ? -step : m.wparam == 0x28 ? step : 0;
            WarpPointer(mouse_x + dx, mouse_y + dy);
            return true;
        }
        return true;
    }
    if (m.message == 0x101 || m.message == 0x105) return true;
    return false;
}
// First refusal on raw input while a tracker owns the pointer.
bool FrameTrackRouteInput(const InputPacket &p)
{
    if (track.kind == TrackNone) return false;
    if (p.kind == 1 || p.kind == 2) AcceptInputMetadata(p);
    if (p.kind == 1)
    {
        Message m{};
        m.hwnd = track.window; m.message = p.message; m.wparam = p.key; m.lparam = p.parameter;
        FrameTrackMessage(m);
        return true;
    }
    if (p.kind != 2) return true;
    if (!IsWindow(track.window)) { EndTrack(true); return true; }
    if (p.moved)
    {
        if (track.kind == TrackScroll) TrackScrollTo(p.x, p.y);
        else TrackTo(p.x, p.y);
    }
    if (p.changed & ~p.buttons & 1) EndTrack(false);
    else if (p.buttons & 2) EndTrack(true); // the right button cancels a drag
    return true;
}

// DefWindowProc's SC_MOVE and SC_SIZE. `command` is the full wParam, so its low
// four bits carry WMSZ for a size drag and HTCAPTION for a caption drag; zero
// there means the system menu or a keyboard entry.
bool BeginSizeMove(HWND h, unsigned command, bool mouse, int x, int y)
{
    auto *w = Find(h);
    if (!w || track.kind != TrackNone || !frame_loop) return false;
    if (w->style & 0x21000000) return false;          // minimized or maximized do not track
    const bool sizing = (command & 0xfff0) == 0xf000;
    const unsigned low = command & 15;
    unsigned edge = sizing && low && low <= 8 ? low : 0;
    if (sizing && !(w->style & 0x40000)) return false; // no WS_THICKFRAME, no SC_SIZE
    const auto o = Origin(*w);
    track = TrackSession{};
    track.kind = sizing ? TrackSize : TrackMove;
    track.window = h;
    track.edge = edge;
    track.origin = track.rect = {o.x, o.y, o.x + w->width, o.y + w->height};
    track.keyboard = !mouse;
    track.anchor_x = x; track.anchor_y = y;
    track.clip = DragLimit(*w);
    ClipDragPointer(&track.clip);
    // Win98 without "show window contents while dragging" draws an inverted
    // frame and moves the window only on release. The local install has
    // DragFullWindows set to "0", so this is the mode the desktop runs in.
    if (!FullDrag()) OutlineBegin(h, *w);
    // USER sends WM_ENTERSIZEMOVE before it takes the capture.
    TrackSend(h, 0x231, 0, 0);
    if (!IsWindow(h) || track.kind == TrackNone) { EndTrack(true); return true; }
    CaptureOperation(1, h, 0);
    // The tracking limits come from WM_GETMINMAXINFO, which the original asks
    // for inside its per-move recompute, after the loop has already started.
    Win98MinMax::Info info = {};
    if (MinMaxDefaults(h, info) && MinMaxAdjust(h, info))
    {
        track.min_width = info.min_track.x; track.min_height = info.min_track.y;
        track.max_width = info.max_track.x; track.max_height = info.max_track.y;
    }
    if (frame_rect)
    {
        Gdi::RECT limits = {track.min_width, track.min_height, track.max_width, track.max_height};
        if (TrackRectMessage(h, 0x24, 0, limits))
        {
            track.min_width = limits.left; track.min_height = limits.top;
            track.max_width = limits.right; track.max_height = limits.bottom;
        }
    }
    if (!IsWindow(h) || track.kind == TrackNone) { EndTrack(true); return true; }
    if (track.keyboard)
    {
        Win98SizeMove::Rect r{track.rect.left, track.rect.top, track.rect.right, track.rect.bottom};
        Win98SizeMove::KeyboardAnchor(r, edge, track.anchor_x, track.anchor_y);
        WarpPointer(track.anchor_x, track.anchor_y);
        if (track.kind == TrackNone) return true;
    }
    track.running = true;
    frame_loop(CallbackContext(frame_context, h));
    if (track.kind != TrackNone) EndTrack(true);
    return true;
}
// DefWindowProc WM_NCLBUTTONDOWN on HTVSCROLL/HTHSCROLL, and the SCROLLBAR
// control's own WM_LBUTTONDOWN (USER, which enters with hit code zero).
bool BeginScrollTrack(HWND h, unsigned vertical, int x, int y)
{
    auto *w = Find(h);
    if (!w || track.kind != TrackNone || !frame_loop) return false;
    namespace S = Win98ScrollTrack;
    const bool control = w->common == 10;
    if (control && (w->style & 0x18)) return false;   // a size box does not scroll
    Win98Scroll::Geometry g;
    Gdi::RECT rect;
    if (control) { if (!ScrollControlGeometry(h, *w, rect, g, vertical)) return false; }
    else if (!ScrollGeometry(h, *w, vertical, Origin(*w), rect, g)) return false;
    const int along = vertical ? y : x, across = vertical ? x : y;
    const unsigned disabled = (w->scroll.disabled >> (vertical * 2)) & 3u;
    const unsigned part = S::Hit(g, along, across, disabled);
    if (part == S::None) return false;
    track = TrackSession{};
    track.kind = TrackScroll;
    track.window = h;
    track.vertical = vertical;
    track.control = control;
    track.part = part;
    track.inside = true;
    track.start_position = track.track_position = w->scroll.bars[vertical].position;
    scroll_record = ScrollRecord{true, h, vertical, track.track_position};
    scroll_press = Win98Scroll::Press{};
    arrow_window = 0; arrow_part = S::None;
    if (part == S::Thumb)
    {
        track.thumb_grab = g.thumb_start - along;
        track.start_pixel = track.thumb_pixel = g.thumb_start;
        track.zone = S::SnapZone(g, vertical, settings.Word(vertical ? 0x2f0 : 0x2f2));
        scroll_thumb_active = true;
        scroll_thumb_window = h;
        scroll_thumb_vertical = vertical;
        scroll_thumb_position = track.track_position;
    }
    else
    {
        track.zone = S::PartRect(g, part, vertical);
        TrackPress(true);
    }
    dirty = true;
    CaptureOperation(1, h, 0);
    if (part != S::Thumb)
    {
        track.next_repeat = MessageClock() + S::FirstDelay(double_click_time);
        TrackStep();
    }
    else TrackScrollMessage(S::SbThumbTrack, track.track_position);
    if (!IsWindow(h) || track.kind == TrackNone) { if (track.kind) EndTrack(true); return true; }
    track.running = true;
    frame_loop(CallbackContext(frame_context, h));
    if (track.kind != TrackNone) EndTrack(true);
    return true;
}
void SetFrameCallbacks(void *context, FrameSend send, FrameRect rect, FrameLoop loop, FrameCommit commit)
{
    frame_context = context; frame_send = send; frame_rect = rect; frame_loop = loop; frame_commit = commit;
}
