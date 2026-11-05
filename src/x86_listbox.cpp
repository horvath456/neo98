// SPDX-License-Identifier: GPL-2.0-or-later
// Native listbox storage/ABI; original USER geometry and queries in win98_listbox.h.
#include "user_draw.h"
#include "win98_listbox.h"
#include "x86_app.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
struct Application::ListBox
{
    static constexpr unsigned Capacity = 1024;
    unsigned hwnd = 0, active = 0, count = 0, version = 0, font = 0, style = 0;
    unsigned current = ~0u, caret = 0, anchor = ~0u, first = 0, height = 13, column_width = 120;
    unsigned extent = 0, horizontal = 0, locale = 0x409;
    // State word 2Ch: the average character width of the list's font, which
    // a line of WM_HSCROLL moves.
    int char_width = 0;
    // LB_SETTABSTOPS: the stops in dialog units, a quarter of the average
    // character width each; none means a stop every 32 units (LBS_USETABSTOPS).
    int tabs[32] = {};
    unsigned tab_count = 0;
    bool deleting = false, tracking = false;
    char *text[Capacity] = {};
    unsigned data[Capacity] = {};
    unsigned char selected[Capacity] = {}, heights[Capacity] = {};
    bool Multiple() const { return (style & 0x808) && !(style & 0x4000); }
    bool Variable() const { return (style & 0x20) && !(style & 0x210); }
    bool Owner() const { return (style & 0x10) || Variable(); }
    bool Strings() const { return !Owner() || (style & 0x40); }
    Win98ListBox::Layout Layout() const
    {
        Gdi::RECT r = {};
        User::GetClientRect(hwnd, &r);
        Win98ListBox::Layout s;
        s.count = count;
        s.first = first;
        s.height = height;
        s.column_width = column_width;
        s.width = r.right;
        s.client_height = r.bottom;
        s.variable = Variable();
        s.multicolumn = style & 0x200;
        s.heights = heights;
        s.default_height = height;
        s.rows = r.bottom / int(height);
        if (s.rows < 1)
            s.rows = 1;
        s.columns = r.right / int(column_width);
        if (s.columns < 1)
            s.columns = 1;
        s.extra_width = horizontal;
        return s;
    }
    bool Selected(unsigned i) const
    {
        return Win98ListBox::Selection(count, current, Multiple(), selected, i, 2) != 0;
    }
    // USER: the number of whole items the list shows (state word
    // 0Eh). A variable list counts them from its last item upwards; a list
    // with fewer than two items counts one.
    int FullItems() const
    {
        const auto s = Layout();
        if (!s.variable)
            return Win98ListBox::Page(s, false);
        if (count < 2)
            return 1;
        int left = s.client_height, n = 0;
        for (int i = int(count) - 1; i >= 0; --i)
        {
            left -= heights[i];
            if (left < 0)
                break;
            ++n;
        }
        return n;
    }
    // USER (the scroll parameters) as Windows 98 runs it after every
    // change of the top item: the range is the item count (the column count
    // of a multi-column list), the page the whole items (columns) the window
    // shows. With its scroll bar the list hands both to SetScrollInfo, which
    // hides the bar when everything fits (LBS_DISABLENOSCROLL disables it
    // instead) and answers the position it allows; without one cuts
    // the position to the same limit. Either way the top item never lies
    // beyond count minus page, which is what keeps a combo box's list from
    // opening with its selection on the top line and empty rows below.
    // `settling` stands for the nesting a frame change of the
    // scroll bar could start through WM_SIZE.
    bool settling = false;
    void Settle()
    {
        if (!hwnd || settling)
            return;
        settling = true;
        const auto s = Layout();
        const bool multi = style & 0x200;
        // the bar flags come from the creation style; a 3.x
        // window with either bar counts both.
        unsigned bars = style & 0x300000;
        if (bars && !User::ModernWindow(hwnd))
            bars = 0x300000;
        const bool present = bars & (multi ? 0x100000 : 0x200000);
        int top = multi ? int(first) / s.rows : int(first);
        int max = multi ? (count ? int(count - 1) / s.rows + 1 : 0) : int(count);
        int page = multi ? s.columns : FullItems();
        if (max)
            --max;
        if (!present)
        {
            int shown = max + 1 < page ? max + 1 : page;
            if (shown < 0)
                shown = 0;
            const int limit = max - (shown ? shown - 1 : 0);
            if (top > limit)
                top = limit;
            if (top < 0)
                top = 0;
        }
        else
        {
            Win98Scroll::Info info{28, (style & 0x1000) ? 0x1fu : 0x17u, 0, unsigned(max),
                                   unsigned(page < 0 ? 0 : page), unsigned(top), 0};
            top = int(User::ScrollState(hwnd, 1, multi ? 0 : 1, &info, 1));
        }
        first = unsigned(multi ? top * s.rows : top);
        settling = false;
    }
    // USER with SB_HORZ for a single-column list: the range is the
    // horizontal extent (46h), the page the width of the client rectangle,
    // the position the origin (44h). With its bar the list hands them to
    // SetScrollInfo, without one cuts the origin to the same limit.
    int LimitHorizontal(int x) const
    {
        Gdi::RECT r = {};
        User::GetClientRect(hwnd, &r);
        const int width = Win98ListBox::Word(r.right - r.left);
        const int range = Win98ListBox::Word(int(extent));
        unsigned bars = style & 0x300000;
        if (bars && !User::ModernWindow(hwnd))
            bars = 0x300000;
        if (!(bars & 0x100000))
            return Win98ListBox::LimitTop(x, range, width);
        Win98Scroll::Info info{28, (style & 0x1000) ? 0x1fu : 0x17u, 0, unsigned(range ? range - 1 : 0),
                               unsigned(width < 0 ? 0 : width), unsigned(x), 0};
        return Win98ListBox::Word(int(User::ScrollState(hwnd, 1, 0, &info, 1)));
    }
    // GetCharDimensions of the font, the average width of
    // the alphabet for a proportional font, tmAveCharWidth otherwise.
    static int CharWidth(unsigned font, unsigned fallback)
    {
        int width = 0;
        const auto dc = Gdi::CreateCompatibleDC();
        if (!dc)
            return 0;
        Gdi::SelectObject(dc, font ? font : fallback);
        Gdi::TEXTMETRICA metrics{};
        if (Gdi::GetTextMetricsA(dc, &metrics))
        {
            width = metrics.tmAveCharWidth;
            Gdi::SIZE size{};
            if ((metrics.tmPitchAndFamily & 1) &&
                Gdi::GetTextExtentPoint32A(dc, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz", 52, &size))
                width = (size.cx / 26 + 1) / 2;
        }
        Gdi::DeleteDC(dc);
        return width;
    }
    void Ensure(unsigned i)
    {
        Place(i);
        Settle();
    }
    void Place(unsigned i)
    {
        if (i >= count)
            return;
        auto s = Layout();
        if (i < first)
            first = s.multicolumn ? i / s.rows * s.rows : i;
        else if (s.multicolumn)
        {
            unsigned page = unsigned(s.rows * s.columns);
            if (i >= first + page)
                first = (i / s.rows - s.columns + 1) * s.rows;
        }
        else
        {
            Gdi::RECT r;
            while (first < i)
            {
                s.first = first;
                Win98ListBox::Rectangle(s, i, r);
                if (r.bottom <= s.client_height)
                    break;
                ++first;
            }
        }
    }
    struct Hold
    {
        ListBox &b;
        explicit Hold(ListBox &p) : b(p) { ++b.active; }
        ~Hold() { --b.active; }
    };
    ~ListBox()
    {
        for (auto *s : text)
            delete[] s;
    }
};
// The rows of every list box, for a test without a screen (step lists=1).
void Application::DumpListBoxes(void (*line)(const char *))
{
    for (auto *box : list_boxes)
    {
        if (!box || !box->hwnd)
            continue;
        char text[600];
        snprintf(text, sizeof text, "listbox hwnd=%x rows=%u first=%u horizontal=%u visible=%u", box->hwnd, box->count,
                 box->first, box->horizontal, User::IsVisible(box->hwnd) ? 1u : 0u);
        line(text);
        for (unsigned i = 0; i < box->count && i < ListBox::Capacity; ++i)
        {
            snprintf(text, sizeof text, "listbox row=%u text=%s", i, box->Strings() && box->text[i] ? box->text[i] : "");
            line(text);
        }
    }
}
Application::ListBox *Application::FindListBox(unsigned hwnd, bool create)
{
    for (auto *b : list_boxes)
        if (b && b->hwnd == hwnd && hwnd)
            return b;
    if (create && hwnd)
        for (auto &b : list_boxes)
            if (!b || (!b->hwnd && !b->active))
            {
                delete b;
                b = new ListBox;
                b->hwnd = hwnd;
                b->style = User::Style(hwnd);
                return b;
            }
    return nullptr;
}
bool Application::InspectListBox(unsigned hwnd, unsigned &count, unsigned &selection) const
{
    for (const auto *box : list_boxes)
        if (box && box->hwnd == hwnd && hwnd)
        { count=box->count; selection=box->current; return true; }
    return false;
}
void Application::FreeListBoxes()
{
    for (auto &b : list_boxes)
    {
        delete b;
        b = nullptr;
    }
}
unsigned Application::ListBoxMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp)
{
    if(ComboListMouse(hwnd,message,wp,lp))return 0;
    auto *ptr = FindListBox(hwnd, message == 0x81 || message == 1);
    if (!ptr)
        return message == 0x81 ? 0 : ~0u;
    auto &b = *ptr;
    ListBox::Hold hold(b);
    auto refresh = [&]()
    {
        if (b.hwnd)
            User::InvalidateRect(hwnd, nullptr, false);
    };
    auto draw_item = [&](unsigned item, unsigned action, bool focus)
    {
        if(!b.hwnd||item>=b.count||!User::IsVisible(hwnd))return;
        if(User::ScreenLockedElsewhere(process_id,ThreadId())){
            refresh();
            return;
        }
        Gdi::RECT client{},row{};User::GetClientRect(hwnd,&client);
        Win98ListBox::Rectangle(b.Layout(),item,row);
        if(row.bottom<=0||row.right<=0||row.top>=client.bottom||row.left>=client.right)return;
        if(const auto dc=User::GetDC(hwnd)){
            const int saved=Gdi::SaveDC(dc);
            if(saved){DrawListBox(hwnd,dc,client,item,action,focus);Gdi::RestoreDC(dc,saved);User::DrawingChanged(dc);}
            User::ReleaseDC(hwnd,dc);
        }
    };
    auto notify = [&](unsigned code)
    {
        // A ComboLBox popup's ordinary owner is the top-level window. Its
        // control notification belongs to the composite ComboBox instead.
        if (b.hwnd && ComboListNotify(hwnd, code)) return;
        if (b.hwnd)
            CallWindow((User::Parent(hwnd)?User::Parent(hwnd):User::Owner(hwnd)), 0x111, User::ControlID(hwnd) | (code << 16), hwnd);
    };
    // USER, the listbox's WM_SIZE. Unless LBS_NOINTEGRALHEIGHT or an
    // owner-drawn variable list set bit 8 of state word 4Eh, the
    // window height minus the border units (including frame and client
    // edge) must be a whole number of items; otherwise SetWindowPos (flags
    // 16h) cuts it to the items that fit. A 3.x window counts SM_CYEDGE per
    // unit once more before dividing. ComboLBox follows this rule
    // too: its actual height may be smaller than CB_GETDROPPEDCONTROLRECT.
    auto integral = [&]()
    {
        if ((b.style & 0x100) || b.Variable() || !b.height || !b.hwnd)
            return;
        Gdi::RECT r = {};
        if (!User::GetWindowRect(hwnd, &r))
            return;
        const unsigned extended = User::ExtendedStyle(hwnd);
        const int units = int(FrameBorderUnits(User::Style(hwnd), extended) + ((extended & 0x200) ? 2 : 0));
        unsigned index = 6;
        const int border = int(Api("GetSystemMetrics", &index));
        index = 46;
        const int edge = int(Api("GetSystemMetrics", &index));
        const int item = int(b.height);
        const int height = r.bottom - r.top - 2 * units * border;
        if (height % item == 0)
            return;
        int rows = height;
        if (!User::ModernWindow(hwnd))
            rows += units * edge;
        rows /= item;
        const unsigned args[] = {hwnd, 0, 0, 0, unsigned(r.right - r.left), unsigned(units * edge + item * rows), 0x16};
        Api("SetWindowPos", args);
    };
    auto owner = [&](unsigned msg, unsigned *values, unsigned bytes)
    {
        unsigned p = Allocate(bytes);
        if (!p)
            return 0u;
        Store(p, values, bytes);
        unsigned target = ComboOfList(hwnd);
        if (!target) target = User::Parent(hwnd) ? User::Parent(hwnd) : User::Owner(hwnd);
        unsigned result = CallWindow(target, msg, User::ControlID(hwnd), p);
        Read(p, values, bytes);
        Free(p);
        return result;
    };
    auto measure = [&](unsigned index, unsigned value)
    {
        unsigned values[] = {2, User::ControlID(hwnd), index, 0, b.height, value};
        unsigned version = b.version;
        owner(0x2c, values, sizeof values);
        if (!b.hwnd || version != b.version)
            return;
        unsigned h = values[4] & 255;
        if (b.Variable())
        {
            if (index < b.count)
                b.heights[index] = h ? h : 1;
        }
        else
        {
            if (h)
                b.height = h;
            if ((b.style & 0x200) && values[3] && values[3] <= 32767)
                b.column_width = values[3];
        }
    };
    auto remove = [&](unsigned i)
    {
        if (i >= b.count)
            return ~0u;
        char *text = b.text[i];
        unsigned value = b.data[i];
        for (unsigned n = i + 1; n < b.count; ++n)
        {
            b.text[n - 1] = b.text[n];
            b.data[n - 1] = b.data[n];
            b.selected[n - 1] = b.selected[n];
            b.heights[n - 1] = b.heights[n];
        }
        b.text[--b.count] = nullptr;
        ++b.version;
        if (b.current == i)
            b.current = ~0u;
        else if (b.current != ~0u && b.current > i)
            --b.current;
        if (b.caret && b.caret >= i)
            --b.caret;
        if (b.anchor != ~0u && b.anchor >= i)
            b.anchor = b.anchor ? b.anchor - 1 : 0;
        if (b.first >= b.count)
            b.first = b.count ? b.count - 1 : 0;
        if (!b.deleting)
            b.Settle();
        delete[] text;
        if (b.Owner())
        {
            unsigned v[] = {2, User::ControlID(hwnd), i, hwnd, value};
            owner(0x2d, v, sizeof v);
        }
        refresh();
        return b.count;
    };
    auto choose = [&](unsigned i, bool keyboard)
    {
        if (i >= b.count)
            return;
        unsigned before = b.current;
        const unsigned version = b.version;
        const bool focused = User::GetFocus() == hwnd;
        if (!b.Multiple() && before != i && focused)
            draw_item(b.caret, 4, false);
        if (!b.hwnd || b.version != version) return;
        if (b.Multiple())
        {
            bool shift = User::Modifiers() & 1, control = User::Modifiers() & 2;
            if ((b.style & 0x800) && shift)
            {
                if (b.anchor == ~0u)
                    b.anchor = b.caret;
                unsigned low = i < b.anchor ? i : b.anchor, high = i > b.anchor ? i : b.anchor;
                for (unsigned n = 0; n < b.count; ++n)
                    if (!control || (n >= low && n <= high))
                        b.selected[n] = n >= low && n <= high ? 0x11 : 0;
            }
            else if (((b.style & 8) && !(b.style & 0x800)) || control)
            {
                if (!keyboard) {
                    b.selected[i] ^= 0x11;
                    b.anchor = i;
                }
            }
            else
            {
                for (unsigned n = 0; n < b.count; ++n)
                    b.selected[n] = n == i ? 0x11 : 0;
                b.anchor = i;
            }
        }
        else {
            b.current = i;
            b.anchor = i;
        }
        b.caret = i;
        b.Ensure(i);
        ++b.version;
        if(!b.Multiple()&&before!=i){
            const unsigned version=b.version;
            draw_item(before, 2, false);
            if(b.hwnd&&b.version==version)draw_item(i, 2, false);
            if(b.hwnd&&b.version==version&&focused)draw_item(i, 4, true);
        }
        refresh();
        if (keyboard && (b.style & 1) && (b.Multiple() || before != i))
            notify(1);
    };
    switch (message)
    {
    case 0x81:
        return 1;
    case 1:
        if (b.style & 0x2000)
        {
            Fail("LBS_NODATA storage is not implemented");
            return ~0u;
        }
        if (b.Owner() && !b.Variable())
            measure(0, 0);
        // a list that draws its own strings starts with the
        // character height of the listbox DC's default font, and Windows 98 ends
        // the creation with WM_SIZE, which applies the integral height with
        // that item height. The default font of a Win98 DC is the System font
        // (SYSTEM.INI [boot] fonts.fon, VGASYS.FON here, 16 pixels high), which
        // a fresh DC of this port selects as well (Gdi::SetDefaultFont).
        // Date/Time relies on the sequence: its AM/PM list of 31 pixels
        // becomes 16 here and 13 after WM_SETFONT, inside the time box.
        if (!b.Owner() || b.Variable())
        {
            Gdi::TEXTMETRICA metrics{};
            const auto dc = Gdi::CreateCompatibleDC();
            if (dc && Gdi::GetTextMetricsA(dc, &metrics) && metrics.tmHeight > 0)
                b.height = unsigned(metrics.tmHeight);
            if (dc)
                Gdi::DeleteDC(dc);
        }
        b.char_width = ListBox::CharWidth(0, system);
        integral();
        // Windows 98 ends the creation with WM_SIZE, which sets the scroll
        // parameters: an empty list starts without its scroll bar.
        b.Settle();
        return 0;
    case 2:
        b.deleting = true;
        while (b.count && b.hwnd)
            remove(b.count - 1);
        return 0;
    case 0x82:
        b.hwnd = 0;
        return 0;
    case 5:
        integral();
        // the new size recounts the whole items and runs
        // with the old top.
        b.Settle();
        refresh();
        return 0;
    case 0xf:
        User::ValidateRect(hwnd, nullptr);
        return 0;
    case 7:
    case 8:
        draw_item(b.caret, 4, message == 7);
        refresh();
        notify(message == 7 ? 4 : 5);
        return 0;
    case 0xb:
        User::SetControlRedraw(hwnd, wp != 0);
        return 0;
    case 0x30:
    {
        b.font = wp;
        b.char_width = ListBox::CharWidth(wp, system);
        bool changed = false;
        if (!b.Owner())
        {
            auto dc = Gdi::CreateCompatibleDC();
            if (dc)
            {
                Gdi::SelectObject(dc, wp ? wp : system);
                Gdi::SIZE s = {};
                Gdi::GetTextExtentPoint32A(dc, "M", 1, &s);
                if (s.cy > 0 && s.cy < 256)
                    changed = b.height != unsigned(s.cy), b.height = s.cy;
                Gdi::DeleteDC(dc);
            }
        }
        // a new item height sizes a 4.0 window again.
        if (changed && User::ModernWindow(hwnd))
            integral();
        if (lp)
            refresh();
        return 0;
    }
    case 0x31:
        return b.font;
    case 0x87:
        // USER: DLGC_WANTARROWS | DLGC_WANTCHARS (81h).
        // WANTALLKEYS would swallow the dialog's Tab, Enter and Escape.
        return 0x81;
    // LB_DIR: the enumeration of USER, in src/x86_dlgdir.cpp. It goes
    // back through LB_ADDSTRING, so the sort style decides the order.
    case 0x18d:
        return ListBoxDirectory(hwnd, wp, lp);
    case 0x180:
    case 0x181:
    {
        if (b.deleting)
            return ~0u;
        if (b.count == ListBox::Capacity)
            return ~1u;
        char *text = nullptr;
        unsigned data = lp;
        if (b.Strings())
        {
            const char *source = ReadString(lp);
            unsigned n = strlen(source);
            text = new char[n + 1];
            memcpy(text, source, n + 1);
            data = 0;
        }
        unsigned index = wp == ~0u || wp > b.count ? b.count : wp;
        if (message == 0x180)
        {
            index = b.count;
            if (b.style & 2)
                for (index = 0; index < b.count; ++index)
                {
                    int order = 0;
                    if (b.Strings())
                    {
                        const unsigned char *a = (const unsigned char *)text,
                                            *c = (const unsigned char *)b.text[index];
                        for (;; ++a, ++c)
                        {
                            unsigned x = *a, y = *c;
                            if (x >= 'A' && x <= 'Z')
                                x += 32;
                            if (y >= 'A' && y <= 'Z')
                                y += 32;
                            if (x != y)
                            {
                                order = x < y ? -1 : 1;
                                break;
                            }
                            if (!x)
                                break;
                        }
                    }
                    else
                    {
                        unsigned version = b.version,
                                 v[] = {2,       User::ControlID(hwnd), hwnd, index, b.data[index], ~0u, data,
                                        b.locale};
                        order = (int)owner(0x39, v, sizeof v);
                        if (!b.hwnd || version != b.version)
                        {
                            delete[] text;
                            return ~0u;
                        }
                        order = order < 0 ? 1 : order > 0 ? -1 : 0;
                    }
                    if (order < 0)
                        break;
                }
        }
        for (unsigned n = b.count; n > index; --n)
        {
            b.text[n] = b.text[n - 1];
            b.data[n] = b.data[n - 1];
            b.selected[n] = b.selected[n - 1];
            b.heights[n] = b.heights[n - 1];
        }
        b.text[index] = text;
        b.data[index] = data;
        b.selected[index] = 0;
        b.heights[index] = b.height;
        ++b.count;
        ++b.version;
        if (b.current != ~0u && b.current >= index)
            ++b.current;
        if (b.anchor != ~0u && b.anchor >= index)
            ++b.anchor;
        if (b.caret >= index && b.count > 1)
            ++b.caret;
        if (b.Variable())
            measure(index, data);
        // after the insertion: the scroll bars follow the count.
        b.Settle();
        refresh();
        return b.hwnd ? index : ~0u;
    }
    case 0x182:
        return remove(wp);
    case 0x184:
        if (b.deleting)
            return 0;
        b.deleting = true;
        while (b.count && b.hwnd)
            remove(b.count - 1);
        b.deleting = false;
        b.current = b.anchor = ~0u;
        b.caret = b.first = 0;
        b.Settle();
        refresh();
        return 0;
    case 0x185:
        if (!b.Multiple() || (lp != ~0u && lp >= b.count))
            return ~0u;
        for (unsigned n = 0; n < b.count; ++n)
            if (lp == ~0u || lp == n)
                b.selected[n] = wp ? 0x11 : 0;
        ++b.version;
        refresh();
        return 0;
    case 0x186:
    {
        if (b.Multiple())
            return ~0u;
        if (wp != ~0u && wp >= b.count)
            return ~0u;
        const unsigned before=b.current;
        const unsigned version=b.version;
        const bool focused=User::GetFocus()==hwnd;
        if(before!=wp&&focused)draw_item(b.caret,4,false);
        if(!b.hwnd||b.version!=version)return wp;
        b.current = wp;
        if (wp != ~0u)
        {
            b.caret = wp;
            b.Ensure(wp);
        }
        ++b.version;
        if(before!=wp){
            const unsigned version=b.version;
            draw_item(before,2,false);
            if(b.hwnd&&b.version==version)draw_item(wp,2,false);
            if(b.hwnd&&b.version==version&&focused)draw_item(b.caret,4,true);
        }
        refresh();
        return wp;
    }
    case 0x187:
        return wp >= b.count ? ~0u
                             : Win98ListBox::Selection(b.count, b.current, b.Multiple(), b.selected, wp, 2);
    case 0x188:
        return b.Multiple() ? b.caret : b.current;
    case 0x189:
    case 0x18a:
        if (wp >= b.count)
            return ~0u;
        if (!b.Strings())
        {
            if (message == 0x189)
                Write32(lp, b.data[wp]);
            return 4;
        }
        if (message == 0x189)
            Store(lp, b.text[wp], strlen(b.text[wp]) + 1);
        return strlen(b.text[wp]);
    case 0x18b:
        return b.count;
    case 0x18c:
    case 0x18f:
    case 0x1a2:
    {
        char search[8192] = {};
        if (b.Strings())
        {
            const char *s = ReadString(lp);
            memcpy(search, s, strlen(s) + 1);
        }
        unsigned start = wp == ~0u ? 0 : wp + 1;
        for (unsigned n = 0; n < b.count; ++n)
        {
            unsigned i = (start + n) % b.count;
            bool match = false;
            if (!b.Strings())
                match = b.data[i] == lp;
            else
            {
                unsigned j = 0;
                for (;; ++j)
                {
                    unsigned a = (unsigned char)search[j], c = (unsigned char)b.text[i][j];
                    if (a >= 'A' && a <= 'Z')
                        a += 32;
                    if (c >= 'A' && c <= 'Z')
                        c += 32;
                    if (!a)
                    {
                        match = message != 0x1a2 || !c;
                        break;
                    }
                    if (a != c)
                        break;
                }
            }
            if (match)
            {
                if (message == 0x18c)
                {
                    if (b.Multiple())
                        b.selected[i] = 0x11;
                    else
                        b.current = i;
                    b.caret = i;
                    b.Ensure(i);
                    refresh();
                }
                return i;
            }
        }
        return ~0u;
    }
    case 0x18e:
        return b.first;
    case 0x190:
    case 0x191:
    {
        if (!b.Multiple())
            return ~0u;
        unsigned count = 0;
        for (unsigned n = 0; n < b.count; ++n)
            if (b.Selected(n))
            {
                if (message == 0x191 && count < wp)
                    Write32(lp + 4 * count, n);
                ++count;
            }
        return message == 0x191 && count > wp ? wp : count;
    }
    case 0x192:
    {
        // LB_SETTABSTOPS: wp stops at lp; none is every 32 dialog units, one
        // is a fixed distance. Without LBS_USETABSTOPS the list keeps none.
        if (!(b.style & 0x80))
            return 0;
        const unsigned count = wp < 32 ? wp : 32;
        if (count && (!lp || !Read(lp, b.tabs, count * 4)))
            return 0;
        b.tab_count = count;
        User::InvalidateRect(hwnd, nullptr, true);
        return 1;
    }
    case 0x193:
        return b.extent;
    case 0x194:
        if (wp > 32767)
            return ~0u;
        // a new extent runs, which with a scroll bar
        // sets the top again and the origin SB_THUMBPOSITION.
        if (b.extent == wp)
            return 0;
        b.extent = wp;
        if (b.style & 0x300000)
        {
            b.Settle();
            if (!(b.style & 0x200))
                b.horizontal = unsigned(b.LimitHorizontal(int(b.horizontal)));
        }
        refresh();
        return 0;
    case 0x195:
        if (!wp || wp > 32767)
            return ~0u;
        b.column_width = wp;
        refresh();
        return 0;
    case 0x197:
        if (wp >= b.count && !(wp == 0 && !b.count))
            return ~0u;
        // Windows 98 takes the new top and the scroll
        // parameters cut it to the last page.
        b.first = (b.style & 0x200) ? wp / b.Layout().rows * b.Layout().rows : wp;
        b.Settle();
        refresh();
        return 0;
    case 0x198:
    {
        Gdi::RECT r = {};
        int result = Win98ListBox::Rectangle(b.Layout(), wp, r);
        Store(lp, &r, sizeof r);
        return result;
    }
    case 0x199:
        return Win98ListBox::Data(b.count, true, b.data, wp);
    case 0x19a:
        if (wp != ~0u && wp >= b.count)
            return ~0u;
        for (unsigned n = 0; n < b.count; ++n)
            if (wp == ~0u || wp == n)
                b.data[n] = lp;
        ++b.version;
        return 1;
    case 0x19b:
    case 0x183:
    {
        if (!b.Multiple())
            return ~0u;
        unsigned low = message == 0x183 ? wp : lp & 65535, high = message == 0x183 ? lp : lp >> 16;
        bool on = message == 0x183 ? low <= high : wp != 0;
        if (low > high)
        {
            unsigned t = low;
            low = high;
            high = t;
        }
        // LB_SELITEMRANGE clips its upper bound to the existing items.
        // A negative low WORD is an out-of-range unsigned index, a no-op.
        if (message == 0x183 && (low >= b.count || high >= b.count))
            return ~0u;
        for (unsigned i = low; i <= high && i < b.count; ++i)
            b.selected[i] = on ? 0x11 : 0;
        ++b.version;
        refresh();
        return 0;
    }
    case 0x19c:
        if (wp >= b.count)
            return ~0u;
        b.anchor = wp;
        return 0;
    case 0x19d:
        return b.anchor;
    case 0x19e:
        if (wp >= b.count)
            return ~0u;
        b.caret = wp;
        b.Ensure(wp);
        refresh();
        return 0;
    case 0x19f:
        return b.caret;
    case 0x1a0:
        if (!(lp & 255) || lp > 255 || (b.Variable() && wp >= b.count))
            return ~0u;
        if (b.Variable())
            b.heights[wp] = lp;
        else
            b.height = lp;
        b.Settle();
        refresh();
        return 0;
    case 0x1a1:
        return b.Variable() && b.count ? (wp < b.count ? b.heights[wp] : ~0u) : b.height;
    case 0x1a5:
        return b.locale;
    case 0x1a8:
        return wp > ListBox::Capacity ? ~1u : ListBox::Capacity;
    case 0x1a9: // LB_ITEMFROMPOINT: nearest item in the visible client area.
    {
        const auto s = b.Layout();
        int x = (short)lp, y = (short)(lp >> 16);
        if (!b.count) return 0x10000;
        const bool outside = x < 0 || y < 0 || x >= s.width || y >= s.client_height;
        if (x >= s.width) x = s.width - 1;
        if (y >= s.client_height) y = s.client_height - 1;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        unsigned index = b.first;
        if (s.multicolumn) index += unsigned(x / s.column_width * s.rows + y / s.height);
        else if (!s.variable) index += unsigned(y / s.height);
        else {
            while (index + 1 < b.count && y >= b.heights[index])
                y -= b.heights[index++];
        }
        if (index >= b.count) index = b.count - 1;
        return index | (outside ? 0x10000u : 0);
    }
    case 0x201:
    case 0x203:
    {
        if(User::GetFocus()!=hwnd)Api("SetFocus",&hwnd);
        if(!b.hwnd)return 0;
        // Native USER notifies the parent after focus, before capture/selection.
        SendFromNative(User::Parent(hwnd)?User::Parent(hwnd):User::Owner(hwnd),0x131,0,lp);
        if(!b.hwnd)return 0;
        User::CaptureOperation(1,hwnd);
        if(!b.hwnd)return 0;
        b.tracking=true;
        int x = (short)lp, y = (short)(lp >> 16);
        auto s = b.Layout();
        for (unsigned i = b.first; i < b.count; ++i)
        {
            Gdi::RECT r;
            Win98ListBox::Rectangle(s, i, r);
            if (x >= r.left && x < r.right && y >= r.top && y < r.bottom)
            {
                choose(i, false);
                if (message == 0x203 && (b.style & 1))
                    notify(2);
                break;
            }
        }
        return 0;
    }
    case 0x202:
    {
        const bool tracking=b.tracking;
        b.tracking=false;
        if(User::ThreadCapture(User::WindowThread(hwnd))==hwnd)User::CaptureOperation(9,hwnd);
        if(tracking&&b.hwnd&&(b.style&1))notify(1);
        return 0;
    }
    case 0x215:
        b.tracking=false;
        return 0;
    case 0x100:
    {
        if (!b.count)
            return 0;
        if (b.style & 0x400)
        {
            int target = (int)CallWindow((User::Parent(hwnd)?User::Parent(hwnd):User::Owner(hwnd)), 0x2e, (wp & 65535) | (b.caret << 16), hwnd);
            if (!b.hwnd || target == -2)
                return 0;
            if (target >= 0)
            {
                choose(target, true);
                return 0;
            }
        }
        int available = Win98ListBox::Page(b.Layout(), false);
        unsigned i = b.caret < b.count ? b.caret : 0, page = available > 0 ? available : 1;
        if (wp == 0x26)
            i = i ? i - 1 : 0;
        else if (wp == 0x28)
            i = (b.style & 0x8000) && b.current == ~0u ? 0 : i + 1 < b.count ? i + 1 : i;
        else if (wp == 0x24)
            i = 0;
        else if (wp == 0x23)
            i = b.count - 1;
        else if (wp == 0x21)
            i = i > page ? i - page : 0;
        else if (wp == 0x22)
            i = i + page < b.count ? i + page : b.count - 1;
        else if ((b.style & 0x200) && (wp == 0x25 || wp == 0x27))
        {
            unsigned rows = b.Layout().rows;
            i = wp == 0x25 ? (i >= rows ? i - rows : 0) : (i + rows < b.count ? i + rows : b.count - 1);
        }
        else if (wp == 0x20 && b.Multiple())
        {
            b.selected[i] ^= 0x11;
            refresh();
            if (b.style & 1)
                notify(1);
            return 0;
        }
        else
            return 0;
        choose(i, true);
        return 0;
    }
    case 0x102:
    {
        if (!b.count || wp < 32 || wp > 255)
            return 0;
        if (b.style & 0x400)
        {
            int target = (int)CallWindow((User::Parent(hwnd)?User::Parent(hwnd):User::Owner(hwnd)), 0x2f, (wp & 65535) | (b.caret << 16), hwnd);
            if (!b.hwnd || target == -2)
                return 0;
            if (target >= 0)
            {
                choose(target, true);
                return 0;
            }
        }
        if (!b.Strings() || (wp == 32 && b.Multiple()))
            return 0;
        unsigned ch = wp;
        if (ch >= 'A' && ch <= 'Z')
            ch += 32;
        for (unsigned n = 1; n <= b.count; ++n)
        {
            unsigned i = (b.caret + n) % b.count, c = (unsigned char)b.text[i][0];
            if (c >= 'A' && c <= 'Z')
                c += 32;
            if (ch == c)
            {
                choose(i, true);
                break;
            }
        }
        return 0;
    }
    case 0x115:
    case 0x114:
    {
        bool horizontal = message == 0x114, multi = b.style & 0x200;
        if (!horizontal && multi)
            return 0;
        unsigned command = wp & 65535;
        const int pos = Win98ListBox::Word(int(wp >> 16));
        if (multi || !horizontal)
        {
            // USER (multi-column, WM_HSCROLL) and (WM_VSCROLL)
            // ask for the new top, which cuts it to the last page
            // (whole columns of a multi-column list). SB_ENDSCROLL and unknown
            // codes keep the top and only settle the scroll bar.
            if (!b.count)
                return 0;
            const auto s = b.Layout();
            int value = multi ? Win98ListBox::ColumnScroll(command, pos, int(b.first), int(b.count), s.rows,
                                                           s.columns)
                              : Win98ListBox::LineScroll(command, pos, int(b.first), int(b.count), b.FullItems(),
                                                         b.Variable() ? b.heights : nullptr, s.client_height);
            if (value < 0)
                value = 0;
            b.first = multi ? unsigned(value) / unsigned(s.rows) * unsigned(s.rows) : unsigned(value);
            b.Settle();
            refresh();
            return 0;
        }
        // USER: a line is the average character width, a page two
        // thirds of the client width, the bottom the extent; with
        // SB_HORZ cuts the origin to extent minus client width.
        // An empty list only sets the scroll parameters, SB_ENDSCROLL runs

        if (!b.count)
        {
            b.LimitHorizontal(int(b.horizontal));
            return 0;
        }
        if (command == 8)
        {
            if (b.style & 0x300000)
            {
                b.Settle();
                b.horizontal = unsigned(b.LimitHorizontal(int(b.horizontal)));
            }
            refresh();
            return 0;
        }
        Gdi::RECT r = {};
        User::GetClientRect(hwnd, &r);
        const int value = Win98ListBox::HorizontalScroll(command, pos, int(b.horizontal), int(b.extent),
                                                         Win98ListBox::Word(r.right), b.char_width);
        const unsigned old = b.horizontal;
        b.horizontal = unsigned(b.LimitHorizontal(value));
        if (b.horizontal != old)
            refresh();
        return 0;
    }
    case 0x20a:
    {
        // WM_MOUSEWHEEL: the list box of Win98 scrolls itself, three lines a
        // notch, a list of columns by its columns; a third of a notch is a
        // line (User::RouteWheel).
        const int lines = WheelLines(hwnd, (int)(short)(wp >> 16));
        const unsigned scroll = (User::Style(hwnd) & 0x200) ? 0x114 : 0x115;
        for (int i = 0, n = lines < 0 ? -lines : lines; i < n && i < 60 && User::IsWindow(hwnd); ++i)
            Send(hwnd, scroll, lines > 0 ? 0 : 1, 0);
        return 0;
    }
    default:
        if (message >= 0x180 && message <= 0x1b3)
        {
            char error[100];
            snprintf(error, sizeof error, "Unsupported listbox message %x", message);
            Fail(error);
            return ~0u;
        }
        // A press on the scroll bar of the list: the list box hands it to
        // DefWindowProc, which makes it SC_VSCROLL (or SC_HSCROLL) and runs
        // the tracking loop; that answers with WM_VSCROLL, handled above.
        // A double click on the bar is a second press there.
        // Without this a click on an arrow, into the trough or on the thumb
        // did nothing; only the wheel scrolled (the list and tree
        // views had the same).
        // NCCALCSIZE also needs the guest RECT provider: leaving it unchanged
        // made SetWindowPos discard the list's border/client-edge inset.
        if (message == 0x83 || message == 0xa1 || message == 0xa3 || message == 0x112)
            return DefaultWindowMessage(hwnd, message, wp, lp);
        return User::DefaultMessage(hwnd, message, wp, lp);
    }
}
void Application::DrawListBox(unsigned hwnd, Gdi::HDC dc, const Gdi::RECT &bounds, unsigned item, unsigned action, bool focus)
{
    auto *ptr = FindListBox(hwnd);
    if (!ptr)
        return;
    auto &b = *ptr;
    ListBox::Hold hold(b);
    unsigned version = b.version;
    Gdi::SelectObject(dc, b.font ? b.font : system);
    auto brush = [&](unsigned color) { return Api("GetSysColorBrush", &color); };
    auto color = [&](unsigned index) { return Api("GetSysColor", &index); };
    unsigned args[] = {dc, hwnd};
    const unsigned combo = ComboOfList(hwnd);
    unsigned background = action==4 ? 0 : SendFromNative(combo ? combo : (User::Parent(hwnd)?User::Parent(hwnd):User::Owner(hwnd)), 0x134, args[0], args[1]);
    if (!b.hwnd)
        return;
    if (version != b.version)
    {
        User::InvalidateRect(hwnd, nullptr, false);
        return;
    }
    if (!background)
        background = brush(5);
    if(item==~0u)Gdi::FillRect(dc, &bounds, background);
    auto s = b.Layout();
    for (unsigned i = item==~0u?b.first:item; i < b.count && b.hwnd && (item==~0u||i==item); ++i)
    {
        Gdi::RECT r;
        Win98ListBox::Rectangle(s, i, r);
        if (r.top >= bounds.bottom || r.left >= bounds.right)
            break;
        if (r.bottom <= 0 || r.right <= 0)
            continue;
        int saved = Gdi::SaveDC(dc);
        if (!saved)
            return;
        // Owner-draw callbacks receive the client's clip, not an item clip.
        if (!b.Owner()) Gdi::IntersectClipRect(dc, r.left, r.top, r.right, r.bottom);
        unsigned state = (b.Selected(i) ? 1 : 0) | (!User::IsEnabled(hwnd) ? 4 : 0) |
                         ((focus && User::GetFocus() == hwnd && i == b.caret) ? 16 : 0);
        if (b.Owner())
        {
            unsigned values[] = {2,
                                 User::ControlID(hwnd),
                                 i,
                                 action,
                                 state,
                                 hwnd,
                                 dc,
                                 (unsigned)r.left,
                                 (unsigned)r.top,
                                 (unsigned)r.right,
                                 (unsigned)r.bottom,
                                 b.data[i]};
            unsigned p = Allocate(sizeof values);
            if (!p)
            {
                Gdi::RestoreDC(dc, saved);
                return;
            }
            Store(p, values, sizeof values);
            SendFromNative(combo ? combo : (User::Parent(hwnd)?User::Parent(hwnd):User::Owner(hwnd)), 0x2b, values[1], p);
            Free(p);
        }
        else
        {
            if(action!=4){
            Gdi::FillRect(dc, &r, brush(state & 1 ? 13 : 5));
            Gdi::SetTextColor(dc, color(state & 1 ? 14 : state & 4 ? 17 : 8));
            Gdi::SetBkMode(dc, Gdi::TRANSPARENT);
            const char *text = b.text[i] ? b.text[i] : "";
            const int left = r.left + 2 - int(b.horizontal);
            if (!(b.style & 0x80) || !strchr(text, '\t'))
                Gdi::TextOutA(dc, left, r.top, text, strlen(text));
            else
            {
                // LBS_USETABSTOPS: each piece after a tab starts at the next
                // stop past the pen, measured from the left of the item.
                Gdi::TEXTMETRICA metrics = {};
                Gdi::GetTextMetricsA(dc, &metrics);
                const int unit = metrics.tmAveCharWidth > 0 ? metrics.tmAveCharWidth : 6;
                int pen = left;
                for (const char *piece = text;;)
                {
                    const char *end = strchr(piece, '\t');
                    const int length = end ? int(end - piece) : int(strlen(piece));
                    Gdi::SIZE size = {0, 0};
                    if (length)
                    {
                        Gdi::TextOutA(dc, pen, r.top, piece, length);
                        Gdi::GetTextExtentPoint32A(dc, piece, length, &size);
                    }
                    pen += size.cx;
                    if (!end)
                        break;
                    int next = pen;
                    if (b.tab_count > 1)
                    {
                        for (unsigned s = 0; s < b.tab_count; ++s)
                        {
                            const int stop = left + (b.tabs[s] < 0 ? -b.tabs[s] : b.tabs[s]) * unit / 4;
                            if (stop > pen) { next = stop; break; }
                        }
                    }
                    else
                    {
                        const int width = (b.tab_count ? (b.tabs[0] > 0 ? b.tabs[0] : 1) : 32) * unit / 4;
                        next = left + ((pen - left) / (width > 0 ? width : 1) + 1) * (width > 0 ? width : 1);
                    }
                    pen = next > pen ? next : pen;
                    piece = end + 1;
                }
            }
            }
            if ((state & 16) || action==4)
            {
                for (int x = r.left; x < r.right; x += 2)
                {
                    Gdi::PatBlt(dc, x, r.top, 1, 1, Gdi::DSTINVERT);
                    Gdi::PatBlt(dc, x, r.bottom - 1, 1, 1, Gdi::DSTINVERT);
                }
                for (int y = r.top + 1; y < r.bottom - 1; y += 2)
                {
                    Gdi::PatBlt(dc, r.left, y, 1, 1, Gdi::DSTINVERT);
                    Gdi::PatBlt(dc, r.right - 1, y, 1, 1, Gdi::DSTINVERT);
                }
            }
        }
        Gdi::RestoreDC(dc, saved);
        if (!b.hwnd)
            return;
        if (version != b.version)
        {
            User::InvalidateRect(hwnd, nullptr, false);
            return;
        }
    }
    // WS_BORDER and WS_EX_CLIENTEDGE belong to WM_NCPAINT. Drawing them
    // here overwrites the first and last item inside the client rectangle.
}

// USER GetListBoxInfo. A multi-column list answers how many items fit
// in one column, every other one the whole item count. An invalid
// handle and a window that is not a list box answer zero.
unsigned Application::ListBoxItemsPerColumn(unsigned hwnd)
{
    if (!User::IsWindow(hwnd)) return 0;
    ListBox *b = FindListBox(hwnd);
    if (!b || !b->hwnd) return 0;
    const auto layout = b->Layout();
    return (b->style & 0x200) ? unsigned(layout.rows) : b->count;
}
} // namespace X86
