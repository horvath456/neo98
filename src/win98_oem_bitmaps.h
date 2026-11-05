// SPDX-License-Identifier: GPL-2.0-or-later
// USER OEM bitmap dimensions, caching, and drawing behavior.
#pragma once
#ifdef NEO98_RUNTIME_CONTENT
#include "windows_runtime_data.h"
#else
#include "../build/generated/user_resources.h"
#endif

namespace Win98Oem {

using U = unsigned;

// The metrics Windows 98 reads out of USER's data segment, by their offsets there.
struct Metrics {
    int scroll_width = 16;      // 02e2h
    int scroll_height = 16;     // 02e4h
    int border_y = 1;           // 02eah
    int scroll_cross_y = 16;    // 0306h
    int scroll_cross_x = 16;    // 0308h
    int size_x = 18;            // 031ah, SM_CXSIZE
    int size_y = 18;            // 031ch, SM_CYSIZE
    int edge_x = 2;             // 0338h, SM_CXEDGE
    int edge_y = 2;             // 06eah
    int small_x = 12;           // 0346h
    int small_y = 12;           // 0348h
    int caption_x = 18;         // 034ah
    int caption_y = 18;         // 034ch
    int menu_extra = 0;         // 0d12h
    int menu_bias = 0;          // 0bd8h
    int menu_scroll = 9;        // 0722h
};

struct Cell { int x = 0, y = 0, cx = 0, cy = 0; };

inline int Word(int value) { return (short)(unsigned short)value; }

// The size of one strip entry, or (0, alias) when the entry shares
// the rectangle of an earlier one.
inline void Size(const Metrics &m, unsigned index, int &cx, int &cy)
{
    const unsigned type = Win98UserResources::StripCalls[index][0];
    const unsigned state = Win98UserResources::StripCalls[index][1];
    switch (type) {
    case 1:                                     // DFC_CAPTION
        if (state & 0x0080) { cx = m.small_x; cy = m.small_y; }
        else if (state & 0x0040) {
            if (m.caption_x == m.size_x && m.caption_y == m.size_y) {
                cx = 0; cy = (int)index - 0x12; return;
            }
            cx = m.caption_x; cy = m.caption_y;
        } else { cx = m.size_x; cy = m.size_y; }
        cx = Word(cx - m.edge_x);
        cy = Word(cy - m.edge_y);
        return;
    case 2:                                     // DFC_MENU
    case 5: {                                   // DFC_POPUPMENU
        // The undocumented scroll pair of a scrolling menu takes the stored
        // menu glyph size; everything else is one odd square.
        int square = m.menu_scroll;
        if (!(state & 0x18))
            square = Word(Word(m.border_y + m.menu_extra + m.menu_bias) & ~1) - 1;
        cx = cy = Word(square);
        return;
    }
    case 3:                                     // DFC_SCROLL
        if (state & 8) { cx = m.scroll_width; cy = m.scroll_height; }
        else if (state & 2) { cx = m.scroll_cross_x; cy = m.scroll_height; }
        else { cx = m.scroll_width; cy = m.scroll_cross_y; }
        return;
    case 4:                                     // DFC_BUTTON
        if ((state & 8) && !(state & 0x0400)) { cx = 0; cy = (int)index - 8; return; }
        cx = cy = 13;
        return;
    default:                                    // the plain colour-index fills
        if (state & 1) { cx = Word(m.size_x * 4); cy = m.size_y; }
        else { cx = cy = m.size_y; }
        return;
    }
}

// Lay the entries out in rows no wider than `width` and answer the
// total height. An entry whose width is zero takes the rectangle of the entry
// its height field names.
inline int Pack(const Metrics &m, int width, Cell *cells)
{
    int x = 0, row_height = 0, y = 0;
    for (unsigned index = 0; index < Win98UserResources::StripCount; ++index) {
        int cx = 0, cy = 0;
        Size(m, index, cx, cy);
        cells[index].cx = cx;
        cells[index].cy = cy;
        if (cx == 0)
            continue;
        if (Word(cx + x) > width) { y = Word(y + row_height); x = 0; row_height = 0; }
        cells[index].x = x;
        cells[index].y = y;
        if (cy > row_height)
            row_height = cy;
        x = Word(x + cx);
    }
    // resolves the aliases once the real entries have their places.
    for (unsigned index = 0; index < Win98UserResources::StripCount; ++index)
        if (cells[index].cx == 0) {
            const int alias = cells[index].cy;
            if (alias >= 0 && (unsigned)alias < Win98UserResources::StripCount)
                cells[index] = cells[alias];
        }
    return Word(y + row_height);
}

// The strip entry a caption button of this state is cached as, or -1. USER's
// frame asks for exactly these four states per button plus the pushed, the
// inactive and the hot variant.
inline int CaptionEntry(unsigned state)
{
    return Win98UserResources::StripIndex(1, state);
}

}   // namespace Win98Oem
