// SPDX-License-Identifier: GPL-2.0-or-later
// Menu-item colors, owner-draw flags, borders, and background selection.
// Rendering and owner callbacks are supplied by the environment.
#pragma once

namespace Win98MenuDraw {
using U = unsigned;

// COLOR_ indices this body reads from USER's table at data 0E4Ch.
enum {
    ColorMenu = 4, ColorMenuText = 7, ColorHighlight = 13, ColorHighlightText = 14,
    ColorBtnFace = 15, ColorBtnShadow = 16, ColorGrayText = 17, ColorBtnHighlight = 20};

// The USER data words this body reads besides the color table. 2B8h and 2BAh
// are the two selection settings, 370h the flag that suppresses the embossed
// gray text, 366h and 368h the units of the menu bar marker.
struct Settings {
    const unsigned *colors = nullptr;
    U selection_a = 0;        // 2BAh
    U selection_b = 0;        // 2B8h
    U plain_gray = 0;         // 370h
    int marker_x = 1;         // 366h
    int marker_y = 3;         // 368h
};

// The fields of the original item and menu records this body examines.
struct Item {
    U owner_draw = 0;         // item byte 1, bit 0
    U state = 0;              // item byte 2: bit 0 MF_GRAYED, bit 7 MFS_HILITE
    U state3 = 0;             // item byte 3: bits 1 and 2 mark, bits 1|2 disable
    U bitmap = 0;             // item word 28h
    int x = 0, y = 0, cx = 0, cy = 0;
};
struct Menu {
    U flags4 = 0;             // menu byte 4, bit 0: this is a menu bar
    U flags5 = 0;             // menu byte 5, bit 1: draw every item grayed
    U font = 0;               // menu word 24h
    bool owner_modern = true; // owner window byte 2Eh, bit 6
};

struct Colors {
    U background = ColorMenu, text = ColorMenuText, flags = 0;
    bool selected = false, fill = false, owner_background = false;
};

inline int Half(int value) { return (short)(unsigned short)value / 2; }

// the decision half. mode is the original's first argument: zero for a
// whole-menu repaint, non-zero when one item is refreshed on its own.
inline Colors ItemColors(const Settings &s, const Item &item, const Menu &menu, U mode)
{
    Colors c;
    c.selected = (item.state & 0x80) && !(item.state3 & 6);
    const bool bar = (menu.flags4 & 1) != 0;
    if (c.selected && (bar || s.selection_a || s.selection_b)) {
        c.background = ColorHighlight;
        c.text = ColorHighlightText;
    }
    // An old style bitmap item, or an owner that is not a 4.0 window, keeps
    // the original's legacy path: no background of its own inside a popup.
    bool legacy = false;
    if (item.bitmap) {
        if (item.bitmap < 0x0c)
            legacy = true;
        if (!menu.owner_modern)
            legacy = true;
    }
    if ((item.state & 1) || ((menu.flags5 & 2) && !legacy)) {
        const unsigned *colors = s.colors;
        if (c.background != ColorHighlight && colors[ColorMenu] == colors[ColorBtnFace] &&
            !s.plain_gray) {
            if (colors[ColorMenu] == colors[ColorBtnShadow] &&
                colors[ColorMenu] == colors[ColorBtnHighlight])
                c.flags |= 0x10;
            else
                c.flags |= (menu.flags5 & 2) ? 0x100u : 0x20u;
        } else if (colors[c.background] == colors[ColorGrayText]) {
            c.flags |= 0x10;
        } else {
            c.text = ColorGrayText;
        }
    }
    if (legacy && !bar)
        return c;                      // no background fill at all
    if (!(mode && !menu.font) && !c.selected)
        return c;                      // and
    c.fill = true;
    c.owner_background = menu.font && c.selected && !bar && !s.selection_a && !s.selection_b;
    return c;
}

// A popup item carries its own three dimensional border: pressed while
// it is selected, raised while it is hot, and erased with the menu background
// once neither holds. Bit 0 of the item's seventh byte remembers that state, so
// the original erases exactly the border it drew before.
struct Edge {
    bool draw = false, owner_background = false;
    U top_left = ColorMenu, bottom_right = ColorMenu;
};
inline Edge ItemEdge(const Settings &s, const Item &item, const Menu &menu, U &edge_state)
{
    Edge e;
    if (item.bitmap && item.bitmap < 0x0c)
        return e;
    if ((menu.flags4 & 1) || s.selection_a || s.selection_b)
        return e;
    if (item.state & 0x80) {
        e.top_left = ColorBtnShadow;
        e.bottom_right = ColorBtnHighlight;
        edge_state |= 1;
        e.draw = true;
    } else if (item.state3 & 1) {
        e.top_left = ColorBtnHighlight;
        e.bottom_right = ColorBtnShadow;
        edge_state |= 1;
        e.draw = true;
    } else if (edge_state & 1) {
        if (menu.font)
            e.owner_background = true;
        edge_state &= ~1u;
        e.draw = true;
    }
    return e;
}
// The four one-pixel runs the border is made of. An owner-supplied background
// brush covers all four; otherwise the first two use the top-left color.
template<class E> void DrawItemEdge(E &e, const Item &item, const Edge &edge)
{
    if (!edge.draw)
        return;
    e.Begin(edge);
    e.Edge(item.x, item.y, item.cx - 1, 1, edge.top_left);
    e.Edge(item.x, item.y, 1, item.cy - 1, edge.top_left);
    e.Middle(edge);
    e.Edge(item.x, item.y + item.cy - 1, item.cx - 1, 1, edge.bottom_right);
    e.Edge(item.x + item.cx - 1, item.y, 1, item.cy, edge.bottom_right);
}

// An open or pressed menu bar item carries a marker: two full-height
// end caps and a half-height bar between them, all in COLOR_HIGHLIGHT. Bit 1 of
// the item's fourth byte puts it at the top, bit 2 at the bottom.
template<class E> bool Marker(E &e, const Settings &s, const Item &item)
{
    const bool top = (item.state3 & 2) != 0;
    if (!top && !(item.state3 & 4))
        return false;
    const int bottom = item.y + item.cy;
    const int full = top ? item.y : bottom - s.marker_y;
    e.Fill(item.x + s.marker_x, full, s.marker_x, s.marker_y);
    const int half = Half(s.marker_y);
    e.Fill(item.x + s.marker_x * 2, top ? item.y : bottom - half,
           item.cx - s.marker_x * 4, half);
    e.Fill(item.x + item.cx - s.marker_x * 2, full, s.marker_x, s.marker_y);
    return true;
}

// One decision for the whole menu: when COLOR_MENU matches both the
// button shadow and the button highlight, every separator edge is drawn flat
// and monochrome.
inline U EdgeFlags(const Settings &s)
{
    return s.colors[ColorMenu] == s.colors[ColorBtnHighlight] &&
           s.colors[ColorMenu] == s.colors[ColorBtnShadow] ? 0xc000u : 0u;
}
}
