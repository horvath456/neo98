// SPDX-License-Identifier: GPL-2.0-or-later
// Scroll and top-item rules of src/win98_listbox.h for tools/test_listbox_columns_original.py.
#include "../src/win98_listbox.h"
using namespace Win98ListBox;
// USER followed without a scroll bar.
extern "C" int listbox_column_scroll(unsigned code, int pos, int top, int count, int rows, int columns)
{
    if (!count)
        return top;
    const int item = ColumnScroll(code, pos, top, count, rows, columns);
    const int column = LimitTop(item / rows, (count - 1) / rows + 1, columns);
    return Word(column * rows);
}
// USER followed; `heights` only for variable item heights.
extern "C" int listbox_line_scroll(unsigned code, int pos, int top, int count, int page,
                                   const unsigned char *heights, int client_height)
{
    if (!count || code == 8)
        return top;
    return LimitTop(LineScroll(code, pos, top, count, page, heights, client_height), count, page);
}
// USER called directly (LB_SETTOPINDEX and the other callers).
extern "C" int listbox_set_top(int multi, int top, int count, int rows, int page)
{
    if (!multi)
        return LimitTop(top, count, page);
    return Word(LimitTop(top / rows, count ? (count - 1) / rows + 1 : 0, page) * rows);
}
// USER followed with SB_HORZ and no scroll bar: the new
// horizontal origin. An empty list and SB_ENDSCROLL keep it.
extern "C" int listbox_horizontal_scroll(unsigned code, int pos, int x, int count, int extent, int width,
                                         int char_width)
{
    if (!count || code == 8)
        return x;
    return LimitTop(HorizontalScroll(code, pos, x, extent, width, char_width), extent, width);
}
