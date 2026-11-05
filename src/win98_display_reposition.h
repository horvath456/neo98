// SPDX-License-Identifier: GPL-2.0-or-later
// Reposition top-level windows after a display-mode change. Preserve old
// monitor/work-area rectangles, find each window's previous monitor and
// compute its new position using sixteen-bit arithmetic. Apply changes
// through DeferWindowPos with SWP_NOZORDER|SWP_NOACTIVATE and respect
// CDS_FULLSCREEN.
#pragma once
#include <stdint.h>

namespace Win98DisplayReposition
{
struct Rect
{
    int left = 0, top = 0, right = 0, bottom = 0;
};
inline int W(int v) { return int16_t(uint16_t(v)); }
inline bool Equal(const Rect &a, const Rect &b)
{
    return W(a.left) == W(b.left) && W(a.top) == W(b.top) && W(a.right) == W(b.right) && W(a.bottom) == W(b.bottom);
}
// USER, IntersectRect: false and an empty result when the two do not
// overlap.
inline bool Intersect(Rect &out, const Rect &a, const Rect &b)
{
    const int left = W(a.left) > W(b.left) ? W(a.left) : W(b.left);
    const int right = W(a.right) < W(b.right) ? W(a.right) : W(b.right);
    const int top = W(a.top) > W(b.top) ? W(a.top) : W(b.top);
    const int bottom = W(a.bottom) < W(b.bottom) ? W(a.bottom) : W(b.bottom);
    if (left >= right || top >= bottom) { out = {}; return false; }
    out = {left, top, right, bottom};
    return true;
}
inline void Offset(Rect &r, int dx, int dy)
{
    r = {W(r.left + dx), W(r.top + dy), W(r.right + dx), W(r.bottom + dy)};
}
// USER, the 16-bit MulDiv: the product plus half the divisor (as an
// unsigned word), divided with truncation; a zero divisor answers the first
// argument.
inline int MulDiv(int number, int numerator, int denominator)
{
    const int c = W(denominator);
    if (!c) return W(number);
    const int32_t product = int32_t(W(number)) * int32_t(W(numerator)) + int32_t(uint16_t(c) >> 1);
    return W(int(product / c));
}
// USER. The index of the old monitor the rectangle lay on: the first
// one that holds it whole, else the one with the largest overlap. -1 for a
// rectangle at least as large as the (new) virtual screen in both directions
// (DS:374h and 376h) and for one that touches no monitor.
inline int MonitorOf(const Rect &r, const Rect *old_monitors, unsigned count, int virtual_width, int virtual_height)
{
    const int width = W(r.right - r.left), height = W(r.bottom - r.top);
    if (width >= W(virtual_width) && height >= W(virtual_height)) return -1;
    int best = -1;
    uint32_t best_area = 0;
    for (unsigned i = 0; i < count; ++i)
    {
        Rect overlap;
        if (!Intersect(overlap, r, old_monitors[i])) continue;
        const int w = W(overlap.right - overlap.left), h = W(overlap.bottom - overlap.top);
        if (w == width && h == height) return int(i);
        const uint32_t area = uint32_t(int32_t(w) * int32_t(h));
        if (area > best_area) { best_area = area; best = int(i); }
    }
    return best;
}
// The mode of for one window: 2 when state bit 40h of byte +2Fh is
// set (edges follow the monitor), 1 for a maximized window that covers the
// old work area (edges follow the work area), else 0 (moved and scaled).
constexpr unsigned MoveScaled = 0, MoveWithWork = 1, MoveWithMonitor = 2;
// USER. `full_screen` is the CDS_FULLSCREEN bit of the change.
inline Rect Place(const Rect &window, const Rect &old_monitor, const Rect &old_work, const Rect &new_monitor,
                  const Rect &new_work, bool full_screen, unsigned mode, unsigned style)
{
    const unsigned high = (style >> 16) & 0xffff;
    // a captioned window without a sizing border is always moved
    // and scaled.
    if (mode && (high & 0xc0) == 0xc0 && !(high & 4)) mode = MoveScaled;
    int dx = W(new_monitor.left - old_monitor.left), dy = W(new_monitor.top - old_monitor.top);
    Rect out = window;
    if (full_screen)
    {
        // only the move of the monitor's origin, and none for a
        // window that was not wholly on its old monitor.
        Rect overlap;
        Intersect(overlap, old_monitor, window);
        if (mode == MoveScaled && !Equal(overlap, window)) dx = dy = 0;
        Offset(out, dx, dy);
        return out;
    }
    if (mode != MoveScaled)
    {
        // each edge moves with the matching edge of the work
        // area or the monitor.
        const Rect &from = mode == MoveWithWork ? old_work : old_monitor;
        const Rect &to = mode == MoveWithWork ? new_work : new_monitor;
        out.left = W(to.left - from.left + window.left);
        out.top = W(to.top - from.top + window.top);
        out.right = W(window.right + (to.right - from.right));
        out.bottom = W(to.bottom - from.bottom + window.bottom);
        return out;
    }
    // a changed monitor size moves the top left corner in
    // proportion.
    const int old_width = W(old_monitor.right - old_monitor.left), old_height = W(old_monitor.bottom - old_monitor.top);
    const int new_width = W(new_monitor.right - new_monitor.left), new_height = W(new_monitor.bottom - new_monitor.top);
    bool scaled = false;
    if (new_width != old_width || new_height != old_height)
    {
        dx = W(dx + MulDiv(window.left - old_monitor.left, new_width - old_width, old_width));
        dy = W(dy + MulDiv(window.top - old_monitor.top, new_height - old_height, old_height));
        scaled = true;
    }
    Offset(out, dx, dy);
    // pushed back into the new work area, right before left
    // and bottom before top.
    const Rect &work = new_work;
    if (W(work.right) < W(out.right)) Offset(out, work.right - out.right, 0);
    if (W(out.left) < W(work.left)) Offset(out, work.left - out.left, 0);
    if (W(out.bottom) > W(work.bottom)) Offset(out, 0, work.bottom - out.bottom);
    if (W(out.top) < W(work.top)) Offset(out, 0, work.top - out.top);
    // a sizable window that still sticks out after a size change is
    // cut to the work area, and one cut on both sides shrinks by one more
    // pixel all round.
    if (scaled && (high & 4))
    {
        unsigned cut = 0;
        if (W(out.right) > W(work.right)) { out.right = W(work.right); cut = 1; }
        if (W(out.bottom) > W(work.bottom)) { out.bottom = W(work.bottom); ++cut; }
        if (cut == 2) out = {W(out.left + 1), W(out.top + 1), W(out.right - 1), W(out.bottom - 1)};
    }
    return out;
}
} // namespace Win98DisplayReposition
