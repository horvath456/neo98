// SPDX-License-Identifier: GPL-2.0-or-later
// New ANSI report ListView. Native DPA functions store row ordering.
#include "native_sort.h"
#include "user_draw.h"
#include "x86_app.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
struct Application::List
{
    struct Item
    {
        unsigned parameter = 0, state = 0, image = ~0u, callback = 0;
        char text[8][256] = {};
    } *items[512] = {};
    struct Column
    {
        int width = 120;
        unsigned format = 0, subitem = 0;
        char text[256] = {};
    } columns[8];
    unsigned hwnd = 0, active = 0, array = 0, count = 0, column_count = 0, font = 0, images[3] = {},
             extended = 0, version = 0, deleting = 0, background = 0xffffff, text_background = 0xffffff,
             foreground = 0, first = 0, row_height = 18;
    struct Hold
    {
        List &list;
        explicit Hold(List &l) : list(l) { ++list.active; }
        ~Hold() { --list.active; }
    };
    ~List()
    {
        for (auto *item : items)
            delete item;
    }
};
// The rows of every list view as the control holds them, for a test without
// a screen (step lists=1 of NEO98_HOST_INPUT). A text the owner supplies on
// demand is not asked for: the dump runs outside the guest.
void Application::DumpLists(void (*line)(const char *))
{
    for (auto *list : lists)
    {
        if (!list || !list->hwnd)
            continue;
        char text[1200];
        snprintf(text, sizeof text, "list hwnd=%x rows=%u columns=%u first=%u visible=%u", list->hwnd, list->count,
                 list->column_count, list->first, User::IsVisible(list->hwnd) ? 1u : 0u);
        line(text);
        for (unsigned i = 0; i < list->count && i < 512; ++i)
        {
            // The order of the rows is the pointer array's (DPA_GetPtr).
            unsigned args[] = {list->array, i};
            const unsigned id = CommonControlApi(332, args);
            const auto *item = id && id <= 512 ? list->items[id - 1] : nullptr;
            if (!item)
                continue;
            int used = snprintf(text, sizeof text, "list row=%u text=%s", i, item->text[0]);
            for (unsigned c = 1; c < 8 && c < (list->column_count ? list->column_count : 1) && used < 900; ++c)
                used += snprintf(text + used, sizeof text - used, " | %s", item->text[c]);
            line(text);
        }
    }
}
Application::List *Application::FindList(unsigned hwnd, bool create)
{
    if (!hwnd)
        return nullptr;
    for (auto *list : lists)
        if (list && list->hwnd == hwnd)
            return list;
    if (create)
        for (auto &list : lists)
            if (!list || (!list->hwnd && !list->active))
            {
                delete list;
                list = new List;
                list->hwnd = hwnd;
                unsigned growth = 8;
                list->array = CommonControlApi(328, &growth);
                if (!list->array)
                {
                    delete list;
                    list = nullptr;
                }
                return list;
            }
    return nullptr;
}
// The height of the column header: none with LVS_NOCOLUMNHEADER (4000h).
namespace
{
int ListHeader(unsigned hwnd) { return (User::Style(hwnd) & 0x4000) ? 0 : 22; }
} // namespace
void Application::FreeLists()
{
    for (auto &list : lists)
    {
        delete list;
        list = nullptr;
    }
}
unsigned Application::ListNotify(List &list, unsigned code, unsigned index, unsigned old_state,
                                 unsigned new_state)
{
    unsigned args[] = {list.array, index}, id = CommonControlApi(332, args);
    auto *item = id && id <= 512 ? list.items[id - 1] : nullptr;
    unsigned data[] = {list.hwnd, User::ControlID(list.hwnd), code, index, 0, new_state, old_state, 8, 0,
                       0,         item ? item->parameter : 0};
    unsigned p = Allocate(sizeof data);
    if (!p)
        return 0;
    Store(p, data, sizeof data);
    unsigned result = CallWindow(User::Parent(list.hwnd), 0x4e, data[1], p);
    Free(p);
    return result;
}
void Application::ListText(List &list, unsigned index, unsigned column, char *out, unsigned capacity)
{
    if (!capacity)
        return;
    out[0] = 0;
    if (column >= 8)
        return;
    unsigned args[] = {list.array, index}, id = CommonControlApi(332, args);
    auto *item = id && id <= 512 ? list.items[id - 1] : nullptr;
    if (!item)
        return;
    const char *text = item->text[column];
    if (item->callback & (1u << column))
    {
        unsigned p = Allocate(52 + 256);
        if (!p)
            return;
        unsigned data[13] = {list.hwnd,
                             User::ControlID(list.hwnd),
                             (unsigned)-150,
                             1,
                             index,
                             column,
                             item->state,
                             ~0u,
                             p + 52,
                             256,
                             item->image,
                             item->parameter,
                             0};
        unsigned version = list.version;
        Store(p, data, sizeof data);
        Write32(p + 52, 0);
        CallWindow(User::Parent(list.hwnd), 0x4e, data[1], p);
        if (!list.hwnd)
        {
            Free(p);
            return;
        }
        Read(p, data, sizeof data);
        text = ReadString(data[8]);
        unsigned n = strlen(text);
        if (n >= capacity)
            n = capacity - 1;
        memcpy(out, text, n);
        out[n] = 0;
        if (version == list.version && (data[3] & 0x1000) && strlen(text) < sizeof item->text[column])
        {
            strcpy(item->text[column], text);
            item->callback &= ~(1u << column);
        }
        Free(p);
        return;
    }
    unsigned n = strlen(text);
    if (n >= capacity)
        n = capacity - 1;
    memcpy(out, text, n);
    out[n] = 0;
}
unsigned Application::ListMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp)
{
    auto *list = FindList(hwnd, message == 0x81 || message == 1);
    if (!list)
    {
        if (message == 0x81 || message == 1) {
            Fail("Cannot allocate native list control");
            return 0;
        }
        // Sizing messages can precede the creation of the control state.
        return DefaultWindowMessage(hwnd, message, wp, lp);
    }
    List::Hold hold(*list);
    // The rows of one page, below the header.
    auto page_rows = [&]() -> unsigned {
        Gdi::RECT r{};
        User::GetClientRect(hwnd, &r);
        const int space = r.bottom - ListHeader(hwnd);
        return space > (int)list->row_height ? space / list->row_height : 1;
    };
    // The vertical bar of the report view (COMCTL32 ListView_RUpdateScrollBars):
    // range over the items, a page of rows, the first row as the position;
    // SetScrollInfo shows the bar when the items do not fit and hides it
    // when they do. LVS_NOSCROLL (2000h) keeps it away.
    auto update_scroll = [&]() {
        const unsigned rows = page_rows();
        if (list->first && list->first + rows > list->count)
            list->first = list->count > rows ? list->count - rows : 0;
        if (User::Style(hwnd) & 0x2000)
            return;
        const unsigned block = Allocate(28);
        if (!block)
            return;
        const unsigned info[7] = {28, 7 /* SIF_RANGE | SIF_PAGE | SIF_POS */, 0,
                                  list->count ? list->count - 1 : 0, rows, list->first, 0};
        Store(block, info, sizeof info);
        const unsigned a[] = {hwnd, 1 /* SB_VERT */, block, 1};
        Api("SetScrollInfo", a);
        Free(block);
    };
    auto refresh = [&]() {
        update_scroll();
        User::InvalidateRect(hwnd, nullptr, false);
    };
    // A new first row from the bar, the wheel or the keyboard.
    auto scroll_to = [&](long first) {
        const unsigned rows = page_rows();
        const long last = list->count > rows ? (long)(list->count - rows) : 0;
        if (first > last) first = last;
        if (first < 0) first = 0;
        if ((unsigned)first == list->first)
            return;
        list->first = (unsigned)first;
        refresh();
    };
    auto item_at = [&](unsigned index) -> List::Item *
    {
        unsigned args[] = {list->array, index};
        unsigned id = CommonControlApi(332, args);
        return id && id <= 512 ? list->items[id - 1] : nullptr;
    };
    auto set_state = [&](unsigned index, unsigned value, unsigned mask)
    {
        auto *item = item_at(index);
        if (!item)
            return false;
        unsigned old = item->state, wanted = (old & ~mask) | (value & mask);
        if (old == wanted)
            return true;
        if (ListNotify(*list, (unsigned)-100, index, old, wanted) || !list->hwnd)
            return false;
        item = item_at(index);
        if (!item)
            return false;
        item->state = wanted;
        ListNotify(*list, (unsigned)-101, index, old, wanted);
        refresh();
        return true;
    };
    switch (message)
    {
    case 0x81:
        return 1;
    case 5: // WM_SIZE: the page changed
        if (list->hwnd && list->count)
            update_scroll();
        return 0;
    case 1:
    case 0xf:
        return 0;
    case 7:
    case 8:
        ListNotify(*list, message == 7 ? (unsigned)-7 : (unsigned)-8, ~0u, 0, 0);
        refresh();
        return 0;
    case 2:
        return ListMessage(hwnd, 0x1009, 0, 0);
    case 0x82:
        CommonControlApi(329, &list->array);
        list->array = 0;
        list->hwnd = 0;
        return 0;
    case 0x30:
        list->font = wp;
        if (lp)
            refresh();
        return 0;
    case 0x31:
        return list->font;
    case 0x87:
        // DLGC_WANTARROWS and DLGC_WANTCHARS, as COMCTL32's list view: Tab,
        // Enter and Esc stay the dialog's.
        return 1 | 0x80;
    case 0xb:
        User::SetControlRedraw(hwnd, wp != 0);
        return 0;
    case 0x1000:
        return list->background;
    case 0x1001:
        // CLR_NONE keeps the background unpainted. The shell's desktop view
        // sets it so the desktop colour USER32!PaintDesktop puts down stays
        // visible behind the icons; the earlier native list view answered
        // white here and covered the desktop.
        list->background = lp;
        refresh();
        return 1;
    case 0x1002:
        return wp < 3 ? list->images[wp] : 0;
    case 0x1003:
    {
        if (wp >= 3 || (lp && !FindImageList(lp)))
            return 0;
        unsigned old = list->images[wp];
        list->images[wp] = lp;
        refresh();
        return old;
    }
    case 0x1004:
        return list->count;
    case 0x1005:
    case 0x1006:
    case 0x1007:
    case 0x102d:
    case 0x102e:
    {
        unsigned data[10] = {};
        Read(lp, data, 36);
        if (message == 0x102d || message == 0x102e)
        {
            data[0] = 1;
            data[1] = wp;
        }
        unsigned index = data[1], column = data[2], mask = data[0];
        if (column >= 8)
            return message == 0x1007 ? ~0u : 0;
        List::Item *item = item_at(index);
        if (message == 0x1007)
        {
            // LVS_SORTASCENDING (10h) and LVS_SORTDESCENDING (20h): the label
            // decides the place, not iItem (COMCTL32 ListView_OnInsertItem
            // looks the text up with the case folded, as lstrcmpi does). The
            // resource lists of SYSDM.CPL insert at -1 into a sorted list

            const unsigned sorted = User::Style(hwnd) & 0x30;
            if (((int)index < 0 && !sorted) || column)
                return ~0u;
            if (index > list->count)
                index = list->count;
            if (sorted && (mask & 1) && data[5] && data[5] != ~0u)
            {
                char label[256];
                snprintf(label, sizeof label, "%s", ReadString(data[5]));
                const auto fold = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + 32u : (unsigned)c; };
                const auto compare = [&](const char *x, const char *y) {
                    for (;; ++x, ++y)
                    {
                        const unsigned l = fold(*x), r = fold(*y);
                        if (l != r)
                            return l < r ? -1 : 1;
                        if (!l)
                            return 0;
                    }
                };
                index = list->count;
                for (unsigned i = 0; i < list->count; ++i)
                {
                    char text[256];
                    ListText(*list, i, 0, text, sizeof text);
                    const int order = compare(text, label);
                    if (sorted & 0x10 ? order > 0 : order < 0)
                    {
                        index = i;
                        break;
                    }
                }
            }
            unsigned id = 0;
            for (unsigned i = 0; i < 512; ++i)
                if (!list->items[i])
                {
                    id = i + 1;
                    break;
                }
            if (!id)
                return ~0u;
            unsigned args[] = {list->array, index, id};
            if (CommonControlApi(334, args) == ~0u)
                return ~0u;
            item = new List::Item;
            list->items[id - 1] = item;
            ++list->count;
            ++list->version;
        }
        if (!item)
            return 0;
        if (message == 0x1005 || message == 0x102d)
        {
            if (mask & 1)
            {
                char text[256];
                ListText(*list, index, column, text, sizeof text);
                PutString(data[5], text, data[6]);
                if (!list->hwnd)
                    return 0;
                if (message == 0x102d)
                    return strlen(text) < data[6] ? strlen(text) : (data[6] ? data[6] - 1 : 0);
                item = item_at(index);
                if (!item)
                    return 0;
            }
            if (mask & 2)
                data[7] = item->image;
            if (mask & 4)
                data[8] = item->parameter;
            if (mask & 8)
                data[3] = item->state & data[4];
            Store(lp, data, 36);
            return 1;
        }
        if (mask & 1)
        {
            if (data[5] == ~0u)
                item->callback |= 1u << column;
            else
            {
                const char *text = ReadString(data[5]);
                if (strlen(text) >= 256)
                {
                    Fail("List item text capacity exceeded");
                    return 0;
                }
                strcpy(item->text[column], text);
                item->callback &= ~(1u << column);
            }
        }
        if (mask & 2)
            item->image = data[7];
        if (mask & 4)
            item->parameter = data[8];
        if (mask & 8)
        {
            if (message == 0x1007)
                item->state = data[3] & data[4];
            else
                set_state(index, data[3], data[4]);
        }
        if (message == 0x1007)
            ListNotify(*list, (unsigned)-102, index, 0, item->state);
        refresh();
        return message == 0x1007 ? index : 1;
    }
    case 0x1008:
    {
        unsigned args[] = {list->array, wp}, id = CommonControlApi(332, args);
        if (!id || id > 512)
            return 0;
        unsigned version = list->version;
        if (!list->deleting)
            ListNotify(*list, (unsigned)-103, wp, 0, 0);
        if (!list->hwnd || version != list->version)
            return 0;
        CommonControlApi(336, args);
        delete list->items[id - 1];
        list->items[id - 1] = nullptr;
        --list->count;
        ++list->version;
        if (list->first >= list->count)
            list->first = 0;
        refresh();
        return 1;
    }
    case 0x1009:
    {
        if (!list->count)
            return 1;
        unsigned old = list->deleting;
        list->deleting = ListNotify(*list, (unsigned)-104, ~0u, 0, 0) != 0;
        if (!list->hwnd)
            return 0;
        while (list->count)
        {
            if (!ListMessage(hwnd, 0x1008, list->count - 1, 0))
            {
                list->deleting = old;
                return 0;
            }
        }
        list->deleting = old;
        return 1;
    }
    case 0x1013: // LVM_ENSUREVISIBLE(index, fPartialOK): the item into the page
    {
        if (wp >= list->count)
            return 0;
        Gdi::RECT r{};
        User::GetClientRect(hwnd, &r);
        const int space = r.bottom - ListHeader(hwnd);
        const unsigned rows = space > (int)list->row_height ? space / list->row_height : 1;
        const unsigned old = list->first;
        if (wp < list->first)
            list->first = wp;
        else if (wp >= list->first + rows)
            list->first = wp - rows + 1;
        if (list->first != old)
            refresh();
        return 1;
    }
    case 0x100d: // LVM_FINDITEMA(start, LVFINDINFOA)
    {
        // LVFI_PARAM (1) compares lParam, LVFI_STRING (2) the label without
        // regard to case, LVFI_PARTIAL (8) its beginning; the search starts
        // after wParam and with LVFI_WRAP (20h) goes on from the top. The
        // nearest-position search (LVFI_NEARESTXY) of icon views has no
        // place in this report view and finds nothing.
        unsigned info[6] = {};
        if (!lp || !Read(lp, info, sizeof info))
            return ~0u;
        const unsigned flags = info[0];
        if (flags & 0x40)
            return ~0u;
        char wanted[256] = {};
        if (flags & 2 && info[1])
            snprintf(wanted, sizeof wanted, "%s", ReadString(info[1]));
        const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? char(c + 32) : c; };
        const auto matches = [&](unsigned i) {
            auto *item = item_at(i);
            if (!item)
                return false;
            if (flags & 1)
                return item->parameter == info[2];
            if (!(flags & 2))
                return false;
            char text[256];
            ListText(*list, i, 0, text, sizeof text);
            unsigned n = 0;
            for (; wanted[n]; ++n)
                if (lower(text[n]) != lower(wanted[n]))
                    return false;
            return (flags & 8) || !text[n];
        };
        const unsigned start = wp == ~0u ? 0 : wp + 1;
        for (unsigned i = start; i < list->count && list->hwnd; ++i)
            if (matches(i))
                return i;
        if (flags & 0x20)
            for (unsigned i = 0; i < start && i < list->count && list->hwnd; ++i)
                if (matches(i))
                    return i;
        return ~0u;
    }
    case 0x100c:
    {
        if (lp & ~15u)
        {
            Fail("Directional ListView item search not implemented");
            return ~0u;
        }
        unsigned start = wp == ~0u ? 0 : wp + 1;
        for (unsigned i = start; i < list->count; ++i)
        {
            auto *item = item_at(i);
            if (item && (item->state & (lp & 15)) == (lp & 15))
                return i;
        }
        return ~0u;
    }
    case 0x1019:
    case 0x101a:
    case 0x101b:
    {
        unsigned data[6] = {};
        Read(lp, data, sizeof data);
        if (wp >= 8)
            return message == 0x101b ? ~0u : 0;
        if (message == 0x101b)
        {
            if (list->column_count == 8)
                return ~0u;
            if (wp > list->column_count)
                wp = list->column_count;
            for (unsigned n = list->column_count; n > wp; --n)
                list->columns[n] = list->columns[n - 1];
            list->columns[wp] = List::Column{};
            list->columns[wp].subitem = wp;
            ++list->column_count;
        }
        if (wp >= list->column_count)
            return 0;
        auto &column = list->columns[wp];
        if (message == 0x1019)
        {
            if (data[0] & 1)
                data[1] = column.format;
            if (data[0] & 2)
                data[2] = column.width;
            if (data[0] & 4)
                PutString(data[3], column.text, data[4]);
            if (data[0] & 8)
                data[5] = column.subitem;
            Store(lp, data, sizeof data);
            return 1;
        }
        if (data[0] & 1)
            column.format = data[1];
        if (data[0] & 2)
            column.width = (int)data[2] < 0 ? 0 : data[2] > 4096 ? 4096 : data[2];
        if (data[0] & 4)
        {
            const char *text = ReadString(data[3]);
            if (strlen(text) >= 256)
                return 0;
            strcpy(column.text, text);
        }
        if (data[0] & 8)
            column.subitem = data[5];
        refresh();
        return message == 0x101b ? wp : 1;
    }
    case 0x101c:
        if (wp >= list->column_count)
            return 0;
        for (unsigned n = wp; n + 1 < list->column_count; ++n)
            list->columns[n] = list->columns[n + 1];
        --list->column_count;
        refresh();
        return 1;
    case 0x101d:
        return wp < list->column_count ? list->columns[wp].width : 0;
    case 0x101e:
        if (wp >= list->column_count)
            return 0;
        if ((short)lp < 0)
        {
            // LVSCW_AUTOSIZE (-1): the widest label of the column, drawn the
            // way DrawList draws it. LVSCW_AUTOSIZE_USEHEADER (-2): the header
            // text; the last column instead fills the rest of the client area
            // (COMCTL32 LVM_SETCOLUMNWIDTH). The modem wizard sizes its model
            // list so.
            const unsigned dc = User::GetDC(hwnd);
            Gdi::SelectObject(dc, list->font ? list->font : system);
            const auto measure = [&](const char *text) {
                Gdi::SIZE size{};
                Gdi::GetTextExtentPoint32A(dc, text, (int)strlen(text), &size);
                return (int)size.cx;
            };
            int width = measure(list->columns[wp].text) + 12;
            if ((short)lp == -2 && wp + 1 == list->column_count)
            {
                Gdi::RECT client{};
                User::GetClientRect(hwnd, &client);
                int used = 2;
                for (unsigned c = 0; c < wp; ++c)
                    used += list->columns[c].width;
                if (client.right - 2 - used > width)
                    width = client.right - 2 - used;
            }
            else
            {
                // Another column fits its widest label, and with
                // LVSCW_AUTOSIZE_USEHEADER its header text as well (the
                // column "Resource type" of the Device Manager).
                const int header = (short)lp == -2 ? width : 0;
                width = 0;
                int inset = 4;
                if (list->columns[wp].subitem == 0)
                {
                    int left = 2;
                    if (auto *state_images = FindImageList(list->images[2]))
                        left += state_images->width;
                    if (auto *images = FindImageList(list->images[1]))
                        left += images->width;
                    inset = left + 2;
                }
                for (unsigned i = 0; i < list->count && list->hwnd; ++i)
                {
                    char text[256];
                    ListText(*list, i, list->columns[wp].subitem, text, sizeof text);
                    const int w = inset + measure(text) + 4;
                    if (w > width)
                        width = w;
                }
                if (!width)
                    width = measure(list->columns[wp].text) + 12;
                if (header > width)
                    width = header;
            }
            User::ReleaseDC(hwnd, dc);
            if (!list->hwnd || wp >= list->column_count)
                return 0;
            list->columns[wp].width = width > 4096 ? 4096 : width;
            refresh();
            return 1;
        }
        list->columns[wp].width = lp & 65535;
        refresh();
        return 1;
    case 0x1023:
        return list->foreground;
    case 0x1024:
        list->foreground = lp;
        refresh();
        return 1;
    case 0x1025:
        return list->text_background;
    case 0x1026:
        list->text_background = lp;
        refresh();
        return 1;
    case 0x1027:
        return list->first;
    case 0x1028:
    {
        Gdi::RECT r;
        User::GetClientRect(hwnd, &r);
        return r.bottom > 22 ? (r.bottom - 22) / list->row_height : 0;
    }
    case 0x102b:
    {
        unsigned data[5] = {};
        Read(lp, data, sizeof data);
        if (wp != ~0u)
            return set_state(wp, data[3], data[4]);
        bool result = true;
        for (unsigned i = 0; i < list->count && list->hwnd; ++i)
            result = set_state(i, data[3], data[4]) && result;
        return result;
    }
    case 0x102c:
    {
        auto *item = item_at(wp);
        return item ? item->state & lp : 0;
    }
    case 0x1030:
    {
        if (!lp)
            return 0;
        unsigned ids[512], data = Read32(list->array + 4), count = list->count;
        for (unsigned i = 0; i < count; ++i)
            ids[i] = Read32(data + i * 4);
        struct Context
        {
            Application *app;
            List *list;
            unsigned procedure, parameter, version;
        } context = {this, list, lp, wp, list->version};
        bool sorted = NativeSort::Sort(ids, count, &context,
                                       [](void *p, unsigned a, unsigned b)
                                       {
                                           auto &c = *static_cast<Context *>(p);
                                           if (!c.list->hwnd || c.list->version != c.version)
                                               return 0;
                                           auto *left = c.list->items[a - 1], *right = c.list->items[b - 1];
                                           unsigned args[] = {left->parameter, right->parameter, c.parameter};
                                           return (int)c.app->Call(c.procedure, args, 3);
                                       });
        if (!sorted || !list->hwnd || list->version != context.version)
            return 0;
        for (unsigned i = 0; i < count; ++i)
            Write32(data + i * 4, ids[i]);
        ++list->version;
        refresh();
        return 1;
    }
    case 0x1032:
    {
        unsigned count = 0;
        for (unsigned i = 0; i < list->count; ++i)
            if (item_at(i)->state & 2)
                ++count;
        return count;
    }
    case 0x1036:
    {
        unsigned old = list->extended;
        list->extended = wp ? (old & ~wp) | (lp & wp) : lp;
        refresh();
        return old;
    }
    case 0x1037:
        return list->extended;
    case 0x2006:
        return 0;
    case 0x2005:
        if (wp)
            Fail("Unicode list notifications not implemented");
        return 0;
    case 0x203:
    {
        int y = (short)(lp >> 16);
        const int top = ListHeader(hwnd);
        if (y < top)
            return 0;
        unsigned index = list->first + (y - top) / list->row_height;
        if (index < list->count)
            ListNotify(*list, (unsigned)-3, index, 0, 0);
        return 0;
    }
    case 0x201:
    {
        int y = (short)(lp >> 16);
        const int top = ListHeader(hwnd);
        if (y < top)
            return 0;
        unsigned index = list->first + (y - top) / list->row_height;
        if (index >= list->count)
            return 0;
        for (unsigned n = 0; n < list->count && list->hwnd; ++n)
            set_state(n, n == index ? 3 : 0, 3);
        // NM_CLICK after the selection; the parent asks LVM_HITTEST where
        // the click was (a check box of state images toggles that way).
        if (list->hwnd)
            ListNotify(*list, (unsigned)-2, index, 0, 0);
        return 0;
    }
    case 0x1012:
    {
        // LVM_HITTEST: LVHITTESTINFO {POINT pt; UINT flags; int iItem}. The
        // state image, the small icon and the label of the first column,
        // in the layout DrawList paints.
        if (!lp)
            return (unsigned)-1;
        int point[2] = {};
        Read(lp, point, 8);
        const int top = ListHeader(hwnd);
        unsigned flags = 1, index = (unsigned)-1; // LVHT_NOWHERE
        if (point[1] >= top)
        {
            const unsigned row = list->first + (point[1] - top) / list->row_height;
            if (row < list->count)
            {
                index = row;
                int x = 2 + 2;
                auto *state_images = FindImageList(list->images[2]);
                auto *images = FindImageList(list->images[1]);
                flags = 4; // LVHT_ONITEMLABEL
                if (state_images && point[0] >= x && point[0] < x + (int)state_images->width)
                    flags = 8; // LVHT_ONITEMSTATEICON
                else
                {
                    if (state_images)
                        x += state_images->width;
                    if (images && point[0] >= x && point[0] < x + (int)images->width)
                        flags = 2; // LVHT_ONITEMICON
                }
            }
        }
        Write32(lp + 8, flags);
        Write32(lp + 12, index);
        return index;
    }
    case 0x100e:
    {
        // LVM_GETITEMRECT(index, RECT with LVIR_* in left), in the layout
        // DrawList paints: the row, the images of the first column, its
        // label. MSPRINT2 asks for it when a manufacturer is picked in Add
        // Printer, to bring the first model into view.
        if (!lp || wp >= list->count)
            return 0;
        const int code = (int)Read32(lp);
        const int top = ListHeader(hwnd) + ((int)wp - (int)list->first) * (int)list->row_height;
        int right = 2;
        for (unsigned c = 0; c < list->column_count; ++c)
            right += list->columns[c].width;
        int icon = 2 + 2, label = icon;
        if (auto *state_images = FindImageList(list->images[2]))
            icon += state_images->width, label = icon;
        if (auto *images = FindImageList(list->images[1]))
            label = icon + images->width;
        const int first_right = list->column_count ? 2 + list->columns[0].width : right;
        int rect[4] = {2, top, right, top + (int)list->row_height};
        if (code == 1) // LVIR_ICON
            rect[0] = icon, rect[2] = label;
        else if (code == 2) // LVIR_LABEL
            rect[0] = label, rect[2] = first_right;
        else if (code == 3) // LVIR_SELECTBOUNDS
            rect[0] = icon;
        Store(lp, rect, sizeof rect);
        return 1;
    }
    case 0x100:
    {
        // LVN_KEYDOWN first: NMLVKEYDOWN {NMHDR; WORD wVKey; UINT flags}.
        {
            unsigned data[] = {list->hwnd, User::ControlID(list->hwnd), (unsigned)-155, wp & 0xffff, 0};
            const unsigned block = Allocate(sizeof data);
            if (block)
            {
                Store(block, data, sizeof data);
                CallWindow(User::Parent(list->hwnd), 0x4e, data[1], block);
                Free(block);
            }
            if (!list->hwnd)
                return 0;
        }
        unsigned focused = ~0u;
        for (unsigned n = 0; n < list->count; ++n)
            if (item_at(n)->state & 1)
            {
                focused = n;
                break;
            }
        unsigned target = focused;
        if (wp == 0x28)
            target = focused == ~0u ? 0 : focused + 1;
        else if (wp == 0x26)
            target = focused == ~0u ? 0 : focused ? focused - 1 : 0;
        else if (wp == 0x24)
            target = 0;
        else if (wp == 0x23)
            target = list->count - 1;
        else if (wp == 0x21 || wp == 0x22) // Page Up, Page Down
        {
            const unsigned rows = page_rows();
            const unsigned from = focused == ~0u ? 0 : focused;
            target = wp == 0x21 ? (from > rows - 1 ? from - (rows - 1) : 0)
                                : (from + rows - 1 < list->count ? from + rows - 1 : list->count - 1);
        }
        if (target < list->count)
        {
            for (unsigned n = 0; n < list->count && list->hwnd; ++n)
                set_state(n, n == target ? 3 : 0, 3);
            if (list->hwnd)
                ListMessage(hwnd, 0x1013, target, 0);
        }
        return 0;
    }
    case 0x102:
        return 0;
    case 0x115: // WM_VSCROLL
    {
        const unsigned rows = page_rows();
        switch (wp & 0xffff)
        {
        case 0: scroll_to((long)list->first - 1); break;          // SB_LINEUP
        case 1: scroll_to((long)list->first + 1); break;          // SB_LINEDOWN
        case 2: scroll_to((long)list->first - (long)rows); break; // SB_PAGEUP
        case 3: scroll_to((long)list->first + (long)rows); break; // SB_PAGEDOWN
        case 4:                                                   // SB_THUMBPOSITION
        case 5: scroll_to((long)(wp >> 16)); break;               // SB_THUMBTRACK
        case 6: scroll_to(0); break;                              // SB_TOP
        case 7: scroll_to((long)list->count); break;              // SB_BOTTOM
        default: break;
        }
        return 0;
    }
    case 0x20a: // WM_MOUSEWHEEL: three rows a notch, a row for a third of it
        scroll_to((long)list->first - WheelLines(hwnd, (int)(short)(wp >> 16)));
        return 0;
    default:
        if (message >= 0x400)
        {
            char error[128];
            snprintf(error, sizeof error, "Unsupported list message %x wp=%x lp=%x", message, wp, lp);
            Fail(error);
            return 0;
        }
        // A press on the scroll bar: DefWindowProc makes it SC_VSCROLL and
        // runs the tracking loop, which answers with WM_VSCROLL.
        // COMCTL32 -> -> delegates NCCALCSIZE
        // to DefWindowProcA. The guest RECT needs its border and scrollbars
        // removed before SetWindowPos commits the new client rectangle.
        if (message == 0x83 || message == 0xa1 || message == 0x112)
            return DefaultWindowMessage(hwnd, message, wp, lp);
        return User::DefaultMessage(hwnd, message, wp, lp);
    }
}
void Application::DrawList(unsigned hwnd, Gdi::HDC dc, const Gdi::RECT &bounds)
{
    auto *list = FindList(hwnd);
    if (!list)
        return;
    List::Hold hold(*list);
    auto fill = [&](Gdi::RECT r, unsigned color)
    {
        auto brush = Gdi::CreateSolidBrush(color);
        Gdi::FillRect(dc, &r, brush);
        Gdi::DeleteObject(brush);
    };
    Gdi::SelectObject(dc, list->font ? list->font : system);
    Gdi::SetBkMode(dc, Gdi::TRANSPARENT);
    if (list->background != ~0u)
        fill(bounds, list->background);
    int x = 2;
    const int top = ListHeader(hwnd);
    for (unsigned c = 0; c < list->column_count && top; ++c)
    {
        const auto &column = list->columns[c];
        Gdi::RECT box = {x, 2, x + column.width, 22};
        fill(box, 0xc0c0c0);
        Win98Draw::ButtonFrame(dc, &box, 0, 0x1000);
        Gdi::SetTextColor(dc, 0);
        Gdi::TextOutA(dc, x + 4, 5, column.text, strlen(column.text));
        x += column.width;
    }
    for (unsigned i = list->first; i < list->count; ++i)
    {
        int y = top + (i - list->first) * list->row_height;
        if (y >= bounds.bottom)
            break;
        unsigned args[] = {list->array, i}, id = CommonControlApi(332, args);
        auto *item = id && id <= 512 ? list->items[id - 1] : nullptr;
        if (!item)
            continue;
        bool selected = item->state & 2;
        fill({2, y, bounds.right - 2, y + (int)list->row_height},
             selected ? 0x800000 : list->text_background);
        Gdi::SetTextColor(dc, selected ? 0xffffff : list->foreground);
        x = 2;
        for (unsigned c = 0; c < list->column_count && c < 8; ++c)
        {
            char text[256];
            ListText(*list, i, list->columns[c].subitem, text, sizeof text);
            if (!list->hwnd)
                return;
            Gdi::SaveDC(dc);
            Gdi::IntersectClipRect(dc, x, y, x + list->columns[c].width, y + list->row_height);
            int inset = 4;
            if (c == 0)
            {
                unsigned current[] = {list->array, i}, current_id = CommonControlApi(332, current);
                auto *current_item = current_id && current_id <= 512 ? list->items[current_id - 1] : nullptr;
                // The state image first (INDEXTOSTATEIMAGEMASK, bits 12 to
                // 15, one based), then the small icon, then the label.
                int left = x + 2;
                if (auto *state_images = FindImageList(list->images[2]))
                {
                    const unsigned state = current_item ? (current_item->state >> 12) & 15 : 0;
                    if (state && state - 1 < state_images->count && state_images->items[state - 1])
                        state_images->items[state - 1]->Draw(dc, left, y + 1);
                    left += state_images->width;
                    inset = left - x + 2;
                }
                auto *images = FindImageList(list->images[1]);
                if (images && current_item && current_item->image < images->count)
                {
                    images->items[current_item->image]->Draw(dc, left, y + 1);
                    inset = left - x + images->width + 2;
                }
            }
            Gdi::TextOutA(dc, x + inset, y + 2, text, strlen(text));
            Gdi::RestoreDC(dc, -1);
            x += list->columns[c].width;
        }
    }
    auto edge = bounds;
    Win98Draw::DrawEdge(dc, &edge, 10, 15);
}
} // namespace X86
