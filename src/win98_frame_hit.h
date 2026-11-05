// SPDX-License-Identifier: GPL-2.0-or-later
// Win98 USER NC_HitTest, the body DefWindowProc runs for WM_NCHITTEST.
// Complete USER. The caller supplies the window rectangle, the
// computed client rectangle, the nonclient flags and the metric words; the two
// original predicates (draw a system-menu icon at all) and (is
// the grow box a sizing corner) are providers.
#pragma once
namespace Win98FrameHit
{
using U = unsigned;
struct Rect { int left, top, right, bottom; };
// The hit test never returns HTTRANSPARENT or HTERROR.
enum Code
{
    Error = ~1u, Transparent = ~0u, Nowhere = 0, Client = 1, Caption = 2, SysMenu = 3,
    Size = 4, Menu = 5, HScroll = 6, VScroll = 7, MinButton = 8, MaxButton = 9,
    Left = 10, Right = 11, Top = 12, TopLeft = 13, TopRight = 14, Bottom = 15,
    BottomLeft = 16, BottomRight = 17, Border = 18, Close = 20, Help = 21
};
// The metric words Windows 98 reads out of USER's data segment.
struct Metrics
{
    int border_x = 1;        // 02E8h SM_CXBORDER
    int border_y = 1;        // 02EAh SM_CYBORDER
    int dialog_x = 3;        // 02ECh SM_CXDLGFRAME
    int dialog_y = 3;        // 02EEh SM_CYDLGFRAME
    int size_x = 18;         // 031Ah SM_CXSIZE
    int size_y = 18;         // 031Ch SM_CYSIZE
    int small_size_x = 16;   // 0346h SM_CXSMSIZE
    int small_size_y = 16;   // 0348h SM_CYSMSIZE
    int caption_y = 19;      // 02E6h SM_CYCAPTION
    int small_caption_y = 17;// 0344h SM_CYSMCAPTION
    int edge_x = 2;          // 0338h SM_CXEDGE
    int edge_y = 2;          // 033Ah SM_CYEDGE
    int scroll_x = 17;       // 02E2h SM_CXVSCROLL
    int scroll_y = 17;       // 02E4h SM_CYHSCROLL
};
// Every rectangle is in screen coordinates, the way the original window record
// carries them. `flags` is the nonclient word at window offset 2Ch: bit 0 menu,
// bit 1 vertical bar, bit 2 horizontal bar, bit 3 caption, bit 22 the Win4
// compatibility flag.
struct Window
{
    U style = 0, extended = 0, flags = 0;
    Rect outer{}, client{};
    U border_units = 0;      // with outer=1 and clientEdge=0
    bool system_icon = true; //
    bool grip_sizes = false; //
};

inline bool In(const Rect &r, int x, int y) { return r.left <= x && x < r.right && r.top <= y && y < r.bottom; }
inline void Inflate(Rect &r, int x, int y) { r.left -= x; r.top -= y; r.right += x; r.bottom += y; }
// USER.
inline U Test(const Metrics &m, const Window &w, int x, int y)
{
    if (!In(w.outer, x, y)) return Nowhere;                             //
    Rect a = w.outer, b;
    bool caption_entry = false;
    if (w.style & 0x20000000)                                           // WS_MINIMIZE,
    {
        Inflate(a, -(m.border_x + m.dialog_x), -(m.border_y + m.dialog_y)); //
        if (!In(a, x, y)) return Caption;                               //
        caption_entry = true;                                           // entry
    }
    if (!caption_entry)
    {
        if (In(w.client, x, y)) return Client;                          //
        // the client rectangle plus its edge and its two bars.
        b = w.client;
        if (w.extended & 0x200) Inflate(b, m.edge_x, m.edge_y);         // WS_EX_CLIENTEDGE
        if (w.flags & 2) b.right += m.scroll_x;
        if (w.flags & 4) b.bottom += m.scroll_y;
        if (In(b, x, y))
        {
            // The client edge itself is border, not bar.
            if (w.extended & 0x200)
            {
                Rect inner = b;
                Inflate(inner, -m.edge_x, -m.edge_y);
                if (!In(inner, x, y)) return Border;
            }
            if (!(w.flags & 4)) return VScroll;                         //
            if (y < w.client.bottom) return VScroll;
            if (!(w.flags & 2)) return HScroll;                         //
            if (x < w.client.right) return HScroll;
            return w.grip_sizes ? BottomRight : Size;                   //
        }
        // the frame is border_units borders wide, never SM_CXFRAME.
        a = w.outer;
        Inflate(a, -int(w.border_units) * m.border_x, -int(w.border_units) * m.border_y);
        if (!In(a, x, y))
        {
            if (!(w.style & 0x40000))                                   // no WS_THICKFRAME,
            {
                if (w.flags & 0x00400000) return Border;
                if (w.style & 0x00400000) return Border;                // WS_DLGFRAME
                if (w.extended & 1) return Border;                      // WS_EX_DLGMODALFRAME
                return Nowhere;
            }
            // the corner zone is one caption button long.
            Rect corner = a;
            const bool tool = (w.extended & 0x80) != 0;
            Inflate(corner, -(tool ? m.small_size_x : m.size_x), -(tool ? m.small_size_y : m.size_y));
            U code = corner.top > y ? 3u : corner.bottom <= y ? 6u : 0u;
            code += corner.left > x ? 1u : corner.right <= x ? 2u : 0u;
            return code + 9;                                            //
        }
        if (b.top <= y) return Nowhere;                                 //
        if ((w.style & 0xc00000) != 0xc00000)                           // no WS_CAPTION,
            return (w.flags & 1) ? Menu : Nowhere;
        if (a.top > y) return Caption;                                  //
    }
    // The caption strip, onward.
    int button;
    if (w.extended & 0x80) { a.top += m.small_caption_y; button = m.small_size_x; }
    else { a.top += m.caption_y; button = m.size_x; }
    if (a.top <= y && (w.flags & 1)) return Menu;                       //
    if (x < a.left || x >= a.right || y >= a.top) return Caption;       //
    if (w.style & 0x80000)                                              // WS_SYSMENU,
    {
        a.left += button;
        if (x < a.left) return w.system_icon ? SysMenu : Caption;
    }
    else if (w.flags & 0x00400000) return Caption;                      //
    a.right -= button;
    if (x >= a.right) return Close;                                     //
    if (w.extended & 0x80) return Caption;                              // tool windows close only
    if (w.style & 0x30000)                                              // WS_MINIMIZEBOX|WS_MAXIMIZEBOX
    {
        a.right -= button;
        if (x >= a.right) return MaxButton;
        a.right -= button;
        if (x >= a.right) return MinButton;
        return Caption;
    }
    if (w.extended & 0x400)                                             // WS_EX_CONTEXTHELP
    {
        a.right -= button;
        if (x >= a.right) return Help;
    }
    return Caption;
}
} // namespace Win98FrameHit
