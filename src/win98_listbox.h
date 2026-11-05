// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_rect.h"
// List-box layout, selection and notification behavior.
namespace Win98ListBox
{
inline int Word(int n) { return (n & 32768) ? (n & 65535) - 65536 : n & 65535; }
struct Layout
{
    int count = 0, first = 0, height = 13, column_width = 120, rows = 1, columns = 1, extra_width = 0;
    int width = 0, client_height = 0, default_height = 13;
    bool variable = false, multicolumn = false;
    const unsigned char *heights = nullptr;
    unsigned Height(unsigned index) const
    {
        if (!count)
            return default_height & 255;
        return index < unsigned(count) && heights ? heights[index] : 0;
    }
};
inline int Page(const Layout &, bool partial);
inline int Rectangle(const Layout &s, unsigned item, Win98::Rect &r)
{
    item &= 65535;
    if (item && item != 65535 && item >= unsigned(s.count))
    {
        r = {};
        return -1;
    }
    const int index = Word(item);
    r = {0, 0, Word(s.width), Word(s.client_height)};
    if (s.multicolumn)
    {
        if (!s.rows)
            return -1; // Invalid provider state would divide by zero in the original.
        r.top = Word((index - (index / s.rows) * s.rows) * s.height);
        r.bottom = Word(r.top + s.height);
        r.left = Word((index / s.rows - s.first / s.rows) * s.column_width);
        r.right = Word(r.left + s.column_width);
    }
    else
    {
        r.right = Word(r.right + s.extra_width);
        if (s.variable)
        {
            if (index < s.first)
            {
                for (int i = index; i < s.first; ++i)
                    r.top = Word(r.top - s.Height(unsigned(i) & 65535));
                r.bottom = Word(r.top + s.Height(item));
                return 0;
            }
            for (int i = s.first; i < index; ++i)
                r.top = Word(r.top + s.Height(unsigned(i) & 65535));
            r.bottom = Word(r.top + (index < s.count ? s.Height(item) : s.height));
            return r.top < Word(s.client_height);
        }
        r.top = Word((index - s.first) * s.height);
        r.bottom = Word(r.top + s.height);
    }
    return index >= s.first && index < Word(s.first + Page(s, true));
}
inline int Page(const Layout &s, bool partial)
{
    if (s.variable)
    {
        for (int i = s.first; i < s.count; ++i)
        {
            Win98::Rect r;
            if (!Rectangle(s, i, r))
                return i - s.first;
            if (!partial && Word(s.client_height) < r.bottom)
                return i - s.first - 1;
        }
        return s.count - s.first;
    }
    if (s.multicolumn)
        return Word((s.columns + unsigned(partial)) * s.rows);
    if (!s.height)
        return 0;
    return Word(s.client_height / s.height + (partial && s.client_height % s.height));
}
inline unsigned Selection(unsigned count, unsigned current, bool multiple, const unsigned char *selected,
                          unsigned index, unsigned which)
{
    index &= 65535;
    if (index >= count)
        return 0;
    return multiple ? (which == 1 ? selected[index] >> 4 : selected[index] & 15) : current == index;
}
inline unsigned Data(unsigned count, bool present, const unsigned *data, unsigned index)
{
    index &= 65535;
    return index >= count ? ~0u : present ? data[index] : 0;
}
// USER for a list without the scroll bar, as Windows 98 runs
// it after every new top: `range` counts the items (the columns of a
// multi-column list), `page` the whole items (columns) the window shows. The
// top never passes range minus page; SetScrollInfo answers the same limit for
// a list with its bar.
inline int LimitTop(int top, int range, int page)
{
    const int max = range ? range - 1 : 0;
    int shown = max + 1 < page ? max + 1 : page;
    if (shown < 0)
        shown = 0;
    const int limit = max - (shown ? shown - 1 : 0);
    top = limit < top ? limit : top;
    return top < 0 ? 0 : top;
}
// Compute the top item after WM_HSCROLL in a multi-column list. A line is one column, a page the columns shown, the thumb
// position counts columns and the bottom is the first item of the last
// column. SB_ENDSCROLL and unknown codes keep the top.
inline int ColumnScroll(unsigned code, int pos, int top, int count, int rows, int columns)
{
    switch (code)
    {
    case 0:
        return Word(top - rows);
    case 1:
        return Word(top + rows);
    case 2:
        return Word(top - rows * columns);
    case 3:
        return Word(top + rows * columns);
    case 4:
    case 5:
        return Word(rows * pos);
    case 6:
        return 0;
    case 7:
        return Word(count - (count - 1) % rows - 1);
    default:
        return top;
    }
}
// USER: the top a page down (up) of a list with variable item
// heights asks for. It adds up the heights from the top down (up) until they
// pass the client height; the new top is the item before the one that did
// not fit, at least one item away. A list the rest fills goes to the end.
inline int VariablePage(const unsigned char *heights, int count, int top, int client_height, bool down)
{
    if (count == 1)
        return 0;
    auto height = [&](int i) { return unsigned(i) < unsigned(count) ? int(heights[i]) : 0; };
    int left = client_height, i = top;
    if (down)
    {
        while (left >= 0 && i < count)
            left -= height(i++);
        if (left >= 0)
            return count - 1;
        return top + 1 < i - 2 ? i - 2 : top + 1;
    }
    while (left >= 0 && i >= 0)
        left -= height(i--);
    if (left >= 0)
        return 0;
    return top - 1 > i + 2 ? i + 2 : top - 1;
}
// Compute the top item after WM_VSCROLL in a single-column list.
// With fixed item heights a page moves by the whole items shown (state word
// 0Eh) less one, at least one; a list with variable heights pages through. The bottom asks for the last item and cuts it.
inline int LineScroll(unsigned code, int pos, int top, int count, int page, const unsigned char *heights = nullptr,
                      int client_height = 0)
{
    const int step = page > 1 ? page - 1 : page;
    switch (code)
    {
    case 0:
        return Word(top - 1);
    case 1:
        return Word(top + 1);
    case 2:
        return heights ? VariablePage(heights, count, top, client_height, false) : Word(top - step);
    case 3:
        return heights ? VariablePage(heights, count, top, client_height, true) : Word(top + step);
    case 4:
    case 5:
        return Word(pos);
    case 6:
        return 0;
    case 7:
        return Word(count - 1);
    default:
        return top;
    }
}
// USER: the horizontal origin WM_HSCROLL of a single-column list
// requests before it is clamped with SB_HORZ. A line is the average
// character width of the list's font (state word 2Ch, GetCharDimensions), a page two thirds of the client width, the bottom
// the horizontal extent (46h). SB_ENDSCROLL and unknown codes keep the origin.
inline int HorizontalScroll(unsigned code, int pos, int x, int extent, int width, int char_width)
{
    switch (code)
    {
    case 0:
        return Word(x - char_width);
    case 1:
        return Word(x + char_width);
    case 2:
        return Word(x + Word(Word(width) / -3 * 2));
    case 3:
        return Word(x + Word(Word(width) / 3 * 2));
    case 4:
    case 5:
        return Word(pos);
    case 6:
        return 0;
    case 7:
        return Word(extent);
    default:
        return x;
    }
}
} // namespace Win98ListBox
