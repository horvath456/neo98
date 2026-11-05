// SPDX-License-Identifier: GPL-2.0-or-later
// Win98 USER cursor decisions: the OEM identifiers, the DefWindowProc
// WM_SETCURSOR choice per hit code, the ShowCursor counter and the clip
// rectangle. The cursor pixels themselves are the original USER.EXE resources;
// see win98_image_resource.h for the OEM identifier to resource mapping.
#pragma once
namespace Win98Cursor
{
using U = unsigned;
// The OEM identifiers USER's own table at segment 21 carries, in its order.
// Resource 100 + index holds the group cursor for entry index.
enum Oem
{
    Arrow = 32512, IBeam = 32513, Wait = 32514, Cross = 32515, UpArrow = 32516,
    SizeNWSE = 32642, SizeNESW = 32643, SizeWE = 32644, SizeNS = 32645,
    SizeAll = 32646, No = 32648, AppStarting = 32650, Help = 32651,
    Pen = 32631, Hand = 32649, Icon = 32641
};
constexpr U Count = 16;
inline U Identifier(U index)
{
    constexpr U table[Count] = {Arrow, IBeam, Wait, Cross, UpArrow, SizeNWSE, SizeNESW, SizeWE,
                                SizeNS, SizeAll, No, AppStarting, Help, Pen, Hand, Icon};
    return index < Count ? table[index] : 0;
}
inline U Index(U identifier)
{
    for (U i = 0; i < Count; ++i)
        if (Identifier(i) == identifier) return i;
    return ~0u;
}
// DefWindowProc WM_SETCURSOR: the arrow a frame hit selects. Zero means the
// window's own class cursor answers instead.
inline U ForHit(U hit)
{
    switch (hit)
    {
    case 10: case 11: return SizeWE;        // HTLEFT, HTRIGHT
    case 12: case 15: return SizeNS;        // HTTOP, HTBOTTOM
    case 13: case 17: return SizeNWSE;      // HTTOPLEFT, HTBOTTOMRIGHT
    case 14: case 16: return SizeNESW;      // HTTOPRIGHT, HTBOTTOMLEFT
    case 4: return SizeNWSE;                // HTGROWBOX
    case 21: return Help;                   // HTHELP
    case 1: return 0;                       // HTCLIENT: the class cursor
    default: return Arrow;
    }
}
// USER's ShowCursor counter. The pointer is drawn while the count is not
// negative; the API returns the new count either way.
struct Counter { int value = 0; };
inline int Show(Counter &c, bool show) { c.value += show ? 1 : -1; return c.value; }
inline bool Visible(const Counter &c) { return c.value >= 0; }
struct Rect { int left, top, right, bottom; };
// ClipCursor: an empty or reversed rectangle clears the clip, and the result is
// intersected with the screen.
inline bool Clip(Rect &clip, bool &active, const Rect *r, const Rect &screen)
{
    if (!r) { active = false; clip = screen; return true; }
    Rect v = *r;
    if (v.left < screen.left) v.left = screen.left;
    if (v.top < screen.top) v.top = screen.top;
    if (v.right > screen.right) v.right = screen.right;
    if (v.bottom > screen.bottom) v.bottom = screen.bottom;
    if (v.right <= v.left || v.bottom <= v.top) { active = false; clip = screen; return false; }
    clip = v; active = true;
    return true;
}
inline void Confine(const Rect &clip, int &x, int &y)
{
    if (x < clip.left) x = clip.left;
    if (y < clip.top) y = clip.top;
    if (x >= clip.right) x = clip.right - 1;
    if (y >= clip.bottom) y = clip.bottom - 1;
}
} // namespace Win98Cursor
