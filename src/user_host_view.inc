// SPDX-License-Identifier: GPL-2.0-or-later
// Host queries for presenting top-level windows separately: visible regions
// from WindowVisible and the drag frame currently inverted on the desktop.
// These functions neither draw nor call into the guest and are not guest
// entry points.

// The visible window region including children, in screen pixels: intersect
// its rectangle and region, then subtract visible windows in front. Return
// the rectangle count even when capacity is smaller. A taskbar hidden by the
// host must stop clipping guest drawing while retaining logical visibility
// for Explorer.
void SetHostHiddenWindow(HWND h)
{
    if (host_hidden_window == h) return;
    host_hidden_window = h;
    GeometryChanged();
}
HWND HostWindowOwner(HWND h)
{
    if (auto *view = PopupForWindow(h)) return view->parent ? view->parent : view->popup.owner;
    const auto *w = Find(h);
    return w ? w->owner : 0;
}

unsigned WindowVisibleRects(HWND h, Gdi::RECT *out, unsigned capacity)
{
    const auto *w = Find(h);
    if (!w || h == Desktop) return 0;
    RectList list;
    // WindowVisible subtracts children for WS_CLIPCHILDREN. Include them here
    // by clearing that bit.
    auto &window = const_cast<Window &>(*w);
    const unsigned style = window.style;
    window.style &= ~0x02000000u;
    WindowVisible(h, WindowScreenRect(window), false, list);
    window.style = style;
    for (unsigned i = 0; i < list.n && i < capacity; ++i) out[i] = list.r[i];
    return list.n;
}
// Current drag-outline rectangle and border dimensions when full-window
// dragging is disabled.
bool DragOutline(Gdi::RECT *rect, int *edge_width, int *edge_height)
{
    if (!track.outlined) return false;
    if (rect) *rect = {track.outline.left, track.outline.top, track.outline.right, track.outline.bottom};
    if (edge_width) *edge_width = track.thickness * settings.metrics[5];
    if (edge_height) *edge_height = track.thickness * settings.metrics[6];
    return true;
}
// The pointer's bounding rectangle within the host image (PointerBox,
// src/user_cursor.inc).
bool PointerDrawn(Gdi::RECT *box)
{
    if (!pointer_drawn) return false;
    int width = 800, height = 600, depth = 32;
    ScreenMode(width, height, depth);
    if (!PublishedDirect(width, height, depth)) return false;
    int left, top, right, bottom;
    PointerBox(pointer_drawn_x, pointer_drawn_y, left, top, right, bottom);
    if (box) *box = {left, top, right, bottom};
    return true;
}
// Whether the full client is awaiting WM_PAINT after showing. WM_ERASEBKGND
// may already have run during ShowWindow; applications that acknowledge it
// without drawing retain the previous pixels.
bool AwaitsPaint(HWND h)
{
    const auto *w = Find(h);
    if (!w || h == Desktop || !w->visible || w->update.count != 1) return false;
    const Gdi::RECT client = Client(*w);
    if (client.left >= client.right || client.top >= client.bottom) return false;
    const auto box = w->update.Bounds();
    return box.left <= client.left && box.top <= client.top && box.right >= client.right && box.bottom >= client.bottom;
}
// Count transitions from hidden to shown so hosts can detect a hide/show
// cycle between observations.
unsigned ShownSerial(HWND h)
{
    const auto *w = Find(h);
    return w ? w->shown : 0;
}
