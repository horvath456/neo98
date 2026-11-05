// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Drag-outline geometry when full-window dragging is disabled. The caller
// draws using PATINVERT and a checkerboard brush. DrawFrame forms four
// non-overlapping edges with thickness scaled by border metrics; FrameEdges
// returns them in left/top/right/bottom order. DrawDragRect inverts only
// differences between old and new edges. When a single rectangle cannot
// represent the difference, invert both full edges instead. Retain the new
// rectangle for the next update.
#include "win98_rect.h"
namespace Win98DragRect
{
using Rect = Win98::Rect;
// `Blt(x, y, width, height)`.
template <class Blt>
void DrawFrame(const Rect &r, int thickness, int cx_border, int cy_border, Blt blt)
{
    const int cx = thickness * cx_border, cy = thickness * cy_border;
    const int w = r.right - cx - r.left, h = r.bottom - cy - r.top;
    blt(r.left, r.top, cx, h);                 //
    blt(r.left + cx, r.top, w, cy);            //
    blt(r.left, r.top + h, w, cy);             //
    blt(r.left + w, r.top + cy, cx, h);        //
}

inline void FrameEdges(const Rect &r, int cx, int cy, Rect out[4])
{
    out[0] = {r.left, r.top + cy, r.left + cx, r.bottom};
    out[1] = {r.left, r.top, r.right - cx, r.top + cy};
    out[2] = {r.right - cx, r.top, r.right, r.bottom - cy};
    out[3] = {r.left + cx, r.bottom - cy, r.right, r.bottom};
}
// `old` becomes `now`.
template <class Blt>
void DrawDragRect(Rect &old, const Rect &now, int thickness, int cx_border, int cy_border, Blt blt)
{
    const int cx = thickness * cx_border, cy = thickness * cy_border;
    Rect before[4], after[4];
    FrameEdges(old, cx, cy, before);
    FrameEdges(now, cx, cy, after);
    for (int i = 0; i < 4; ++i)
    {
        Rect grown{}, gone{};
        const bool fresh = Win98::SubtractRect(&grown, &after[i], &before[i]) != 0;
        const bool stale = Win98::SubtractRect(&gone, &before[i], &after[i]) != 0;
        if (fresh && stale &&
            Win98::EqualRect(&grown, &after[i]) + Win98::EqualRect(&gone, &before[i]) == 1)
        {
            grown = after[i];
            gone = before[i];
        }
        if (fresh) blt(grown.left, grown.top, grown.right - grown.left, grown.bottom - grown.top);
        if (stale) blt(gone.left, gone.top, gone.right - gone.left, gone.bottom - gone.top);
    }
    old = now;
}
} // namespace Win98DragRect
