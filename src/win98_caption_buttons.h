// SPDX-License-Identifier: GPL-2.0-or-later
// Windows 98 caption-button sizing and placement. Calculate button rectangles,
// command IDs, and pressed-state slots from window styles and system metrics.
#pragma once
#include "gdi.h"

namespace Win98CaptionButton {
using U = unsigned;
inline int W(int value) { return (short)(unsigned short)value; }

// The caption-button slot indices used by Windows 98.
enum { Minimize = 2, Maximize = 3, Help = 4, Close = 5 };
// Nonclient hit codes.
enum { HitMinButton = 8, HitMaxButton = 9, HitClose = 20, HitHelp = 21 };

// The hit code chooses the slot and the bit the window stores at 38h
// while the mouse holds the button down.
inline U Slot(U hit)
{
    return hit == HitMinButton ? U(Minimize) : hit == HitMaxButton ? U(Maximize)
         : hit == HitClose ? U(Close) : hit == HitHelp ? U(Help) : 0u;
}
inline U PressedBit(U hit)
{
    return hit == HitMinButton ? 0x2000u : hit == HitMaxButton ? 0x4000u
         : hit == HitClose ? 0x8000u : hit == HitHelp ? 0x1000u : 0u;
}

// USER metric words 2E8h, 2EAh, 2ECh, 2EEh, 31Ah, 31Ch, 338h, 33Ah, 346h, 348h.
struct Metrics {
    int border_x = 1, border_y = 1;      // SM_CXBORDER, SM_CYBORDER
    int dialog_x = 3, dialog_y = 3;      // SM_CXDLGFRAME, SM_CYDLGFRAME
    int size_x = 18, size_y = 18;        // SM_CXSIZE, SM_CYSIZE
    int small_x = 12, small_y = 12;      // SM_CXSMSIZE, SM_CYSMSIZE
    int edge_x = 2, edge_y = 2;          // SM_CXEDGE, SM_CYEDGE
};

struct Result {
    // The caller's rectangle. Even when the slot is absent the original leaves
    // the frame-inset window rectangle behind and returns its origin.
    Gdi::RECT rect{};   // screen coordinates
    U command = 0;      // the WM_SYSCOMMAND value, SC_MINIMIZE and friends
    U cell = 0;         // the OEM bitmap index the original would blit
    bool disabled = false; // set by the painter: the box is drawn inactive
    int x = 0, y = 0;   // the position the original returns, window relative
};

// Style bits: WS_MAXIMIZEBOX 10000h, WS_MINIMIZEBOX 20000h,
// WS_MAXIMIZE 1000000h, WS_MINIMIZE 20000000h. Extended bits:
// WS_EX_TOOLWINDOW 80h, WS_EX_CONTEXTHELP 400h.
inline bool Measure(const Metrics &m, const Gdi::RECT &window, U style, U extended,
                    U slot, U border_units, bool close_enabled, Result &out)
{
    out = Result{};
    const bool minimized = (style & 0x20000000u) != 0;
    const int frame_x = minimized ? m.dialog_x : W(int(border_units) * m.border_x);
    const int frame_y = minimized ? m.dialog_y : W(int(border_units) * m.border_y);
    Gdi::RECT r{W(window.left + frame_x), W(window.top + frame_y),
                W(window.right - frame_x), W(window.bottom - frame_y)};
    int x = W(-window.left), y = W(-window.top);
    out.rect = r;
    out.x = x;
    out.y = y;
    const bool tool = (extended & 0x80u) != 0;
    const int width = tool ? m.small_x : m.size_x;
    const int height = tool ? m.small_y : m.size_y;
    if (slot == Close) {
        if (!close_enabled)
            return false;
        out.cell = tool ? 0x29u : 0u;
        out.command = 0xf060;                       // SC_CLOSE
    } else if (slot == Minimize) {
        // lays the slot out only with WS_MINIMIZEBOX. The painter
        // draws both boxes when either is present and marks the missing
        // one inactive; CaptionButtons (src/user.cpp) does that.
        if (!(style & 0x20000u))
            return false;
        r.right = W(r.right - width * 2);
        x = W(x + m.edge_x);
        out.cell = minimized ? 8u : 4u;
        out.command = minimized ? 0xf120u : 0xf020u;    // SC_RESTORE / SC_MINIMIZE
    } else if (slot == Maximize) {
        if (!(style & 0x10000u))                        //
            return false;
        r.right = W(r.right - width);
        const bool maximized = (style & 0x01000000u) != 0;
        out.cell = maximized ? 8u : 0x0eu;
        out.command = maximized ? 0xf120u : 0xf030u;    // SC_RESTORE / SC_MAXIMIZE
    } else {
        if (!(extended & 0x400u))
            return false;
        r.right = W(r.right - width);
        out.cell = 0x0bu;
        out.command = 0xf180;                       // SC_CONTEXTHELP
    }
    const int top = r.top;
    r.bottom = W(r.top + height);
    r.left = W(r.right - width);
    x = W(x + r.left);
    y = W(y + W(top + m.edge_y));
    r.left = W(r.left - m.border_x);
    r.top = W(r.top - m.border_y);
    r.right = W(r.right + m.border_x);
    r.bottom = W(r.bottom + m.border_y);
    out.rect = r;
    out.x = x;
    out.y = y;
    return true;
}

// The DFC_CAPTION state for the cell the original chose: close, minimize,
// maximize, restore, help.
inline U CaptionState(U cell)
{
    return cell == 4 ? 1u : cell == 0x0e ? 2u : cell == 8 ? 3u : cell == 0x0b ? 4u : 0u;
}
}
