// SPDX-License-Identifier: GPL-2.0-or-later
// Position the BUTTON caption and focus rectangle within its content area.
#pragma once
namespace Win98ButtonLayout {
struct Rect { int left, top, right, bottom; };
struct Metrics
{
    int edge_x, edge_y;     // SM_CXEDGE, SM_CYEDGE (DS:332h, 334h)
    int border_x, border_y; // SM_CXBORDER, SM_CYBORDER (DS:2E2h, 2E4h)
    int check;              // the width of the check cell (DS:9B4h), 13
};
// Signed halving as Windows 98 does it: CDQ, SUB AX,DX, SAR AX,1.
inline int Half(int value) { return value < 0 ? -((-value) >> 1) : value >> 1; }
// a push button answers 1, BS_DEFPUSHBUTTON 3, BS_PUSHLIKE 1, any
// other kind 0.
inline unsigned PushKind(unsigned style)
{
    const unsigned kind = style & 15;
    return kind == 0 ? 1u : kind == 1 ? 3u : (style & 0x1000) ? 1u : 0u;
}
// the alignment bits BS_LEFT/BS_RIGHT (low two) and BS_TOP/BS_BOTTOM
// (next two). When one of them is missing, a push button centres and every
// other button aligns left, and both centre vertically.
inline unsigned Alignment(unsigned style)
{
    unsigned horizontal = (style >> 8) & 3, vertical = (style >> 8) & 0xc;
    if (horizontal && vertical) return horizontal | vertical;
    if (!horizontal) horizontal = PushKind(style) ? 3u : 1u;
    if (!vertical) vertical = 0xc;
    return horizontal | vertical;
}
// with the kind table : push buttons (mode 5) inset the
// client by the edge, BS_DEFPUSHBUTTON by one border more; check
// boxes and radio buttons (mode 2) give the check cell its place at
// the left, with BS_LEFTTEXT at the right, and a window of a 4.0 module half
// the width of the digit '0' more (`zero` is that width). Other kinds keep
// the client.
inline Rect Content(unsigned style, bool win40, Rect client, int zero, const Metrics &m)
{
    Rect r = client;
    const unsigned kind = style & 15;
    if (PushKind(style))
    {
        const int extra = (PushKind(style) & 2) ? 1 : 0;
        r.left += m.edge_x + extra * m.border_x;
        r.top += m.edge_y + extra * m.border_y;
        r.right -= m.edge_x + extra * m.border_x;
        r.bottom -= m.edge_y + extra * m.border_y;
    }
    else if (kind >= 2 && kind <= 6 || kind == 9)
    {
        const int space = m.check + (win40 ? (zero >> 1) : 0);
        if (style & 0x20) r.right -= space;
        else r.left += space;
    }
    return r;
}
// The caption's upper left corner inside the
// content rectangle. `height` is the extent the caption is measured with;
// text counts one point more. A pressed button moves its caption by
// one border.
inline void Caption(unsigned style, Rect content, int width, int height, bool pressed, const Metrics &m,
                    int &x, int &y)
{
    const unsigned a = Alignment(style);
    x = (a & 3) == 1 ? content.left + m.edge_x
      : (a & 3) == 2 ? content.right - width - m.edge_x
                     : Half(content.right - width + content.left);
    y = (a & 0xc) == 4 ? content.top + m.border_y
      : (a & 0xc) == 8 ? content.bottom - height - m.border_y
                       : Half(content.top - height + content.bottom);
    if (pressed) { x += m.border_x; y += m.border_y; }
}
// the focus rectangle. The paint draws it with the
// caption while BST_FOCUS is set. A group box draws none, an owner
// drawn button gets WM_DRAWITEM with ODA_FOCUS instead. A push
// button, BS_DEFPUSHBUTTON and BS_PUSHLIKE frame their content rectangle
// inset by one border, BS_USERBUTTON its client,
// any other button its caption, one border before and one edge after it,
// inside the client. `x`, `y`, `width` and `height` are those
// of Caption. The answer false means nothing is drawn.
inline bool Focus(unsigned style, Rect client, Rect content, int x, int y, int width, int height,
                  const Metrics &m, Rect &out)
{
    const unsigned kind = style & 15;
    if (kind == 7 || kind == 11) return false;
    if (PushKind(style))
    {
        out = {content.left + m.border_x, content.top + m.border_y, content.right - m.border_x,
               content.bottom - m.border_y};
        return true;
    }
    if (kind == 8) { out = client; return true; }
    int top = y - m.border_y;
    if (top < client.top) top = client.top;
    int bottom = top + height + m.edge_y;
    if (bottom > client.bottom) bottom = client.bottom;
    int left = x - m.border_x;
    if (left < client.left) left = client.left;
    int right = left + width + m.edge_x;
    if (right > client.right) right = client.right;
    out = {left, top, right, bottom};
    return true;
}
}
