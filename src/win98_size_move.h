// SPDX-License-Identifier: GPL-2.0-or-later
// Win98 USER move and size tracking: which edges a WMSZ code moves and how the
// tracking limits from WM_GETMINMAXINFO clamp the rectangle.
#pragma once
namespace Win98SizeMove
{
using U = unsigned;
struct Rect { int left, top, right, bottom; };
// WMSZ_ codes, the wParam of WM_SIZING.
enum Edge { None = 0, Left = 1, Right = 2, Top = 3, TopLeft = 4, TopRight = 5,
            Bottom = 6, BottomLeft = 7, BottomRight = 8 };
inline bool MovesLeft(U e) { return e == Left || e == TopLeft || e == BottomLeft; }
inline bool MovesRight(U e) { return e == Right || e == TopRight || e == BottomRight; }
inline bool MovesTop(U e) { return e == Top || e == TopLeft || e == TopRight; }
inline bool MovesBottom(U e) { return e == Bottom || e == BottomLeft || e == BottomRight; }
// The mapping DefWindowProc uses when it turns a hit code into SC_SIZE's low
// bits: SC_SIZE + WMSZ. HTGROWBOX sizes like the bottom right corner.
inline U EdgeFromHit(U hit)
{
    switch (hit)
    {
    case 10: return Left; case 11: return Right; case 12: return Top; case 13: return TopLeft;
    case 14: return TopRight; case 15: return Bottom; case 16: return BottomLeft;
    case 17: case 4: return BottomRight;
    default: return None;
    }
}
// A size drag moves only the named edges; the opposite ones stay where the
// press put them. A move drag translates the whole rectangle.
inline void Apply(Rect &out, const Rect &origin, U edge, int dx, int dy)
{
    out = origin;
    if (!edge) { out.left += dx; out.right += dx; out.top += dy; out.bottom += dy; return; }
    if (MovesLeft(edge)) out.left += dx;
    if (MovesRight(edge)) out.right += dx;
    if (MovesTop(edge)) out.top += dy;
    if (MovesBottom(edge)) out.bottom += dy;
}
// WM_GETMINMAXINFO's ptMinTrackSize and ptMaxTrackSize. The edge that the drag
// moves is the one that gives way; a zero maximum means no maximum.
inline void Limit(Rect &r, U edge, int min_width, int min_height, int max_width, int max_height)
{
    if (!edge) return;
    int width = r.right - r.left, height = r.bottom - r.top;
    int wanted_width = width, wanted_height = height;
    if (wanted_width < min_width) wanted_width = min_width;
    if (max_width > 0 && wanted_width > max_width) wanted_width = max_width;
    if (wanted_height < min_height) wanted_height = min_height;
    if (max_height > 0 && wanted_height > max_height) wanted_height = max_height;
    if (wanted_width != width)
    {
        if (MovesLeft(edge)) r.left = r.right - wanted_width;
        else if (MovesRight(edge)) r.right = r.left + wanted_width;
    }
    if (wanted_height != height)
    {
        if (MovesTop(edge)) r.top = r.bottom - wanted_height;
        else if (MovesBottom(edge)) r.bottom = r.top + wanted_height;
    }
}
// The keyboard variant starts at the middle of the edge the arrow key names and
// then tracks with the caret keys. USER warps the pointer there first.
inline void KeyboardAnchor(const Rect &r, U edge, int &x, int &y)
{
    x = (r.left + r.right) / 2;
    y = (r.top + r.bottom) / 2;
    if (MovesLeft(edge)) x = r.left;
    if (MovesRight(edge)) x = r.right - 1;
    if (MovesTop(edge)) y = r.top;
    if (MovesBottom(edge)) y = r.bottom - 1;
}
// SC_SIZE with no edge yet: the first arrow key picks the edge.
inline U EdgeFromKey(U edge, U key)
{
    const bool horizontal = key == 0x25 || key == 0x27;   // VK_LEFT / VK_RIGHT
    const bool vertical = key == 0x26 || key == 0x28;     // VK_UP / VK_DOWN
    if (!edge) return horizontal ? (key == 0x25 ? Left : Right) : vertical ? (key == 0x26 ? Top : Bottom) : None;
    if (vertical)
    {
        const U side = MovesLeft(edge) ? Left : MovesRight(edge) ? Right : None;
        const U wanted = key == 0x26 ? Top : Bottom;
        if (side == Left) return wanted == Top ? TopLeft : BottomLeft;
        if (side == Right) return wanted == Top ? TopRight : BottomRight;
        return wanted;
    }
    if (horizontal)
    {
        const U side = MovesTop(edge) ? Top : MovesBottom(edge) ? Bottom : None;
        const U wanted = key == 0x25 ? Left : Right;
        if (side == Top) return wanted == Left ? TopLeft : TopRight;
        if (side == Bottom) return wanted == Left ? BottomLeft : BottomRight;
        return wanted;
    }
    return edge;
}
} // namespace Win98SizeMove
