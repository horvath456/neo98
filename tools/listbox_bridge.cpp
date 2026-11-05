// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_listbox.h"
extern "C" int listbox_geometry(const int *v, const unsigned char *heights, unsigned operation, unsigned item,
                                int *rect)
{
    Win98ListBox::Layout s;
    s.count = v[0];
    s.first = v[1];
    s.height = v[2];
    s.column_width = v[3];
    s.rows = v[4];
    s.columns = v[5];
    s.extra_width = v[6];
    s.width = v[7];
    s.client_height = v[8];
    s.default_height = v[9];
    s.variable = v[10];
    s.multicolumn = v[11];
    s.heights = heights;
    if (operation == 1)
        return Win98ListBox::Page(s, item != 0);
    Win98::Rect r;
    int result = Win98ListBox::Rectangle(s, item, r);
    rect[0] = r.left;
    rect[1] = r.top;
    rect[2] = r.right;
    rect[3] = r.bottom;
    return result;
}
extern "C" unsigned listbox_query(unsigned count, unsigned current, unsigned flags,
                                  const unsigned char *selected, const unsigned *data, unsigned item,
                                  unsigned operation)
{
    return operation == 3 ? Win98ListBox::Data(count, flags & 8, data, item)
                          : Win98ListBox::Selection(count, current, flags & 0x60, selected, item, operation);
}
