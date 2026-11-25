// SPDX-License-Identifier: GPL-2.0-or-later
// The Customize Toolbar dialog of the toolbar (TB_CUSTOMIZE, and a double
// click beside the buttons of a CCS_ADJUSTABLE toolbar), TB_SETROWS and
// TB_GETBUTTONTEXT. Included by src/x86_toolbar.inc.
//
// The dialog does what the one of COMMCTRL.DLL of Windows 98 does (its
// AdjustDlgProc, template 200). "Toolbar buttons" (list 203) has one item
// per button of the toolbar and a blank item at the end to insert before;
// "Available buttons" (list 201) starts with a separator and has every button
// the parent offers with TBN_GETBUTTONINFO (TBN_ADJUSTINFO of the 16-bit
// side) that the toolbar does not show, in the parent's order. The item data
// of both lists is the image of the button in the low word (FFFFh for a
// separator) and in the high word the index the parent gave the button, with
// 8000h for an item that cannot be removed (TBN_QUERYDELETE said no, or the
// blank item), 4000h for a hidden button and 2000h for a separator. Add and
// Remove move the selected item from one list into the other, Move Up and
// Move Down move it inside the toolbar's list to the next place TBN_QUERYINSERT
// allows, and every move changes the toolbar at once and ends in
// TBN_TOOLBARCHANGE. Reset sends TBN_RESET and fills the lists again, Help
// sends TBN_CUSTHELP and Close ends the dialog. The lists are drawn by the
// dialog: a button face with the image beside the text.
// Not ported: dragging items between the lists (MakeDragList) and the context
// help of the controls.
namespace X86 {
namespace {
constexpr unsigned CustomizeAvailable = 201, CustomizeReset = 202, CustomizeCurrent = 203,
                   CustomizeRemove = 204, CustomizeHelp = 205, CustomizeUp = 206, CustomizeDown = 207;
constexpr unsigned ItemFixed = 0x8000, ItemHidden = 0x4000, ItemSeparator = 0x2000, ItemIndex = 0x1fff;
constexpr unsigned TbnQueryInsert = 0xfffffd3e, TbnQueryDelete = 0xfffffd3d, TbnToolbarChange = 0xfffffd3c,
                   TbnReset = 0xfffffd3f, TbnCustomHelp = 0xfffffd3b, TbnBeginAdjust = 0xfffffd41,
                   TbnEndAdjust = 0xfffffd40, TbnGetButtonInfo = 0xfffffd44;
constexpr unsigned LbAddString = 0x180, LbInsertString = 0x181, LbDeleteString = 0x182, LbResetContent = 0x184,
                   LbSetCurSel = 0x186, LbGetCurSel = 0x188, LbGetText = 0x189, LbGetTextLen = 0x18a,
                   LbGetCount = 0x18b, LbGetTopIndex = 0x18e, LbSetHorizontalExtent = 0x194,
                   LbGetHorizontalExtent = 0x193, LbSetTopIndex = 0x197, LbGetItemRect = 0x198,
                   LbGetItemData = 0x199, LbSetItemData = 0x19a;
} // namespace

Application::Toolbar *Application::ToolbarOf(unsigned hwnd)
{
    for (auto &t : toolbars)
        if (hwnd && t.hwnd == hwnd && t.object)
            return &t;
    return nullptr;
}
// A notification with an NMTOOLBAR: the item, and the button when the item is
// one of the toolbar's.
unsigned Application::ToolbarItemNotify(unsigned toolbar, unsigned code, unsigned index)
{
    Toolbar *t = ToolbarOf(toolbar);
    const unsigned note = t ? Allocate(64) : 0;
    if (!note)
        return 0;
    for (unsigned i = 0; i < 64; i += 4)
        Write32(note + i, 0);
    Write32(note + 12, index);
    const unsigned get[] = {toolbar, 0x417, index, note + 16}; // TB_GETBUTTON
    if (int(index) >= 0)
        Api("SendMessageA", get);
    ToolbarMemory e(*this, *t);
    const unsigned answer = e.Notify(t->object, code, note);
    Free(note);
    return answer;
}
// TBN_GETBUTTONINFO: the button the parent offers at `index`, as a TBBUTTON
// of twenty bytes, and its text.
bool Application::ToolbarButtonInfo(unsigned toolbar, unsigned index, unsigned button, unsigned text,
                                    unsigned capacity)
{
    Toolbar *t = ToolbarOf(toolbar);
    const unsigned note = t ? Allocate(64) : 0;
    if (!note)
        return false;
    for (unsigned i = 0; i < 64; i += 4)
        Write32(note + i, 0);
    Write32(note + 12, index);
    Write32(note + 36, text ? capacity : 0);
    Write32(note + 40, text);
    if (text && capacity)
        Write32(text, 0);
    ToolbarMemory e(*this, *t);
    const bool given = e.Notify(t->object, TbnGetButtonInfo, note) != 0;
    if (given && button)
        for (unsigned i = 0; i < 20; i += 4)
            Write32(button + i, Read32(note + 16 + i));
    Free(note);
    return given;
}
// The next place at or around `index` where TBN_QUERYINSERT allows a button:
// upwards to `limit` with direction 1, downwards to zero with 2, both
// alternating with 3. Minus one when there is none.
int Application::ToolbarInsertPlace(unsigned toolbar, int index, int limit, unsigned directions)
{
    for (int up = index, down = index;; ++up, --down)
    {
        bool tried = false;
        if ((directions & 1) && up <= limit)
        {
            tried = true;
            if (ToolbarItemNotify(toolbar, TbnQueryInsert, unsigned(up)))
                return up;
        }
        if ((directions & 2) && down >= 0)
        {
            tried = true;
            if (ToolbarItemNotify(toolbar, TbnQueryInsert, unsigned(down)))
                return down;
        }
        if (!tried)
            return -1;
    }
}
unsigned Application::ToolbarCustomize(unsigned hwnd, unsigned position)
{
    Toolbar *t = ToolbarOf(hwnd);
    if (!t || t->customize)
        return 0;
    if (!Win16Process())
    {
        Fail("Toolbar message 041b is not ported yet");
        return 0;
    }
    const unsigned owner = Read32(t->object + 4);
    t->customize = ~0u; // until WM_INITDIALOG names the dialog
    t->customize_position = position;
    ToolbarItemNotify(hwnd, TbnBeginAdjust, ~0u);
    if (state != Failed && ToolbarOf(hwnd))
        Win16ToolbarCustomizeDialog(owner, hwnd);
    if (Toolbar *again = ToolbarOf(hwnd))
    {
        again->customize = 0;
        ToolbarItemNotify(hwnd, TbnEndAdjust, ~0u);
    }
    return 0;
}
// InitAdjustmentLists: both lists from the toolbar and the parent.
bool Application::ToolbarCustomizeLists(unsigned dialog, unsigned toolbar, unsigned position)
{
    auto list = [&](unsigned id, unsigned message, unsigned wp, unsigned lp) {
        const unsigned a[] = {dialog, id, message, wp, lp};
        return Api("SendDlgItemMessageA", a);
    };
    const unsigned count_args[] = {toolbar, 0x418, 0, 0}; // TB_BUTTONCOUNT
    const unsigned count = Api("SendMessageA", count_args);
    const int start = ToolbarInsertPlace(toolbar, int(position), int(count), 3);
    if (start < 0)
        return false;
    const unsigned scratch = Allocate(512);
    if (!scratch)
        return false;
    const unsigned empty = scratch, button = scratch + 4, text = scratch + 32;
    Write32(empty, 0);
    list(CustomizeCurrent, LbResetContent, 0, 0);
    list(CustomizeAvailable, LbResetContent, 0, 0);
    for (unsigned i = 0; i < count; ++i)
    {
        unsigned flags = ToolbarItemNotify(toolbar, TbnQueryDelete, i) ? 0 : ItemFixed;
        const unsigned get[] = {toolbar, 0x417, i, button};
        Api("SendMessageA", get);
        unsigned char pair[2] = {};
        Read(button + 8, pair, 2);
        if (pair[0] & 8)
            flags |= ItemHidden;
        unsigned image = Read32(button) & 0xffff;
        if (pair[1] & 1)
        {
            if (!Read32(button + 4))
                flags |= ItemSeparator;
            image = 0xffff;
        }
        if (list(CustomizeCurrent, LbAddString, 0, empty) != i)
        {
            Free(scratch);
            return false;
        }
        list(CustomizeCurrent, LbSetItemData, i, image | flags << 16);
    }
    if (list(CustomizeCurrent, LbAddString, 0, empty) == count)
        list(CustomizeCurrent, LbSetItemData, count, 0xffffu | (ItemFixed | ItemSeparator) << 16);
    if (list(CustomizeAvailable, LbAddString, 0, empty) == ~0u)
    {
        Free(scratch);
        return false;
    }
    list(CustomizeAvailable, LbSetItemData, 0, 0xffffu | ItemSeparator << 16);
    // The parent's buttons: the text of those on the toolbar goes to their
    // items there, the others into the list of available buttons.
    const unsigned hwnd_current = User::GetDlgItem(dialog, CustomizeCurrent);
    const unsigned font_args[] = {hwnd_current, 0x31, 0, 0}; // WM_GETFONT
    const unsigned font = Api("SendMessageA", font_args);
    const unsigned dc = Gdi::CreateCompatibleDC(0);
    const unsigned old_font = dc && font ? Gdi::SelectObject(dc, font) : 0;
    int widest = 0;
    for (unsigned index = 0; index < 0x2000 && ToolbarButtonInfo(toolbar, index, button, text, 128); ++index)
    {
        unsigned char pair[2] = {};
        Read(button + 8, pair, 2);
        const unsigned command = Read32(button + 4);
        if ((pair[1] & 1) && !command)
            continue;
        char label[128];
        Read(text, label, sizeof label);
        label[sizeof label - 1] = 0;
        Gdi::SIZE size{};
        if (dc && Gdi::GetTextExtentPoint32A(dc, label, int(strlen(label)), &size) && size.cx > widest)
            widest = size.cx;
        const unsigned find[] = {toolbar, 0x419, command, 0}; // TB_COMMANDTOINDEX
        const int at = int(Api("SendMessageA", find));
        if (at < 0)
        {
            if (pair[0] & 8)
                continue;
            const unsigned item = list(CustomizeAvailable, LbAddString, 0, text);
            if (item == ~0u)
                continue;
            const unsigned image = (pair[1] & 1) ? 0xffffu : Read32(button) & 0xffff;
            list(CustomizeAvailable, LbSetItemData, item, image | index << 16);
        }
        else
        {
            const unsigned data = list(CustomizeCurrent, LbGetItemData, unsigned(at), 0);
            list(CustomizeCurrent, LbDeleteString, unsigned(at), 0);
            if (list(CustomizeCurrent, LbInsertString, unsigned(at), text) != unsigned(at))
                break;
            list(CustomizeCurrent, LbSetItemData, unsigned(at), data | index << 16);
        }
    }
    if (old_font)
        Gdi::SelectObject(dc, old_font);
    if (dc)
        Gdi::DeleteDC(dc);
    Free(scratch);
    Toolbar *t = ToolbarOf(toolbar);
    const unsigned extent = unsigned(widest) + (t ? Read32(t->object + 0x70) : 0) + 3;
    list(CustomizeAvailable, LbSetHorizontalExtent, extent, 0);
    list(CustomizeCurrent, LbSetHorizontalExtent, extent, 0);
    list(CustomizeAvailable, LbSetCurSel, 0, 0);
    list(CustomizeCurrent, LbSetCurSel, unsigned(start), 0);
    const unsigned changed[] = {dialog, 0x111, CustomizeCurrent | 1u << 16, hwnd_current}; // LBN_SELCHANGE
    Api("SendMessageA", changed);
    return true;
}
// After a new selection in the toolbar's list: Add needs a place to insert,
// Remove, Move Up and Move Down an item that may go.
void Application::ToolbarCustomizeButtons(unsigned dialog)
{
    Toolbar *found = nullptr;
    for (auto &t : toolbars)
        if (t.object && t.customize == dialog)
            found = &t;
    if (!found)
        return;
    const unsigned toolbar = found->hwnd;
    const unsigned current = User::GetDlgItem(dialog, CustomizeCurrent);
    auto enable = [&](unsigned id, bool on) {
        const unsigned control = User::GetDlgItem(dialog, id);
        if (!on && User::GetFocus() == control)
        {
            const unsigned next[] = {dialog, 0x28, current, 1}; // WM_NEXTDLGCTL
            Api("SendMessageA", next);
        }
        const unsigned a[] = {control, on ? 1u : 0u};
        Api("EnableWindow", a);
    };
    const unsigned get[] = {current, LbGetCurSel, 0, 0};
    const int selected = int(Api("SendMessageA", get));
    enable(1, ToolbarItemNotify(toolbar, TbnQueryInsert, unsigned(selected)) != 0);
    const unsigned data_args[] = {current, LbGetItemData, unsigned(selected), 0};
    const bool removable = !((Api("SendMessageA", data_args) >> 16) & ItemFixed);
    enable(CustomizeRemove, removable);
    enable(CustomizeUp, removable && ToolbarInsertPlace(toolbar, selected - 1, 0, 2) >= 0);
    const unsigned count_args[] = {toolbar, 0x418, 0, 0};
    const int count = int(Api("SendMessageA", count_args));
    enable(CustomizeDown, removable && ToolbarInsertPlace(toolbar, selected + 2, count, 1) >= 0);
}
// Moves item `from_index` of list `from` to place `to_index` of list `to`,
// and the toolbar along with it.
void Application::ToolbarCustomizeMove(unsigned dialog, unsigned from, unsigned from_index, unsigned to,
                                       int to_index, unsigned select_next)
{
    unsigned toolbar = 0;
    for (auto &t : toolbars)
        if (t.object && t.customize == dialog)
            toolbar = t.hwnd;
    if (!toolbar)
        return;
    const unsigned source = User::GetDlgItem(dialog, from), target = User::GetDlgItem(dialog, to);
    auto send = [&](unsigned hwnd, unsigned message, unsigned wp, unsigned lp) {
        const unsigned a[] = {hwnd, message, wp, lp};
        return Api("SendMessageA", a);
    };
    const unsigned data = send(source, LbGetItemData, from_index, 0);
    const unsigned flags = data >> 16;
    if (int(from_index) < 0 || (flags & ItemFixed))
        return;
    if (to == CustomizeCurrent && !ToolbarItemNotify(toolbar, TbnQueryInsert, unsigned(to_index)))
        return;
    const unsigned length = send(source, LbGetTextLen, from_index, 0);
    const unsigned text = Allocate(length + 1 + 64);
    if (!text)
        return;
    const unsigned button = text + ((length + 4) & ~3u);
    send(source, LbGetText, from_index, text);
    send(target, 0xb, 0, 0); // WM_SETREDRAW
    unsigned top = send(target, LbGetTopIndex, 0, 0);
    bool failed = false;
    if (to == CustomizeAvailable)
    {
        // A separator is always offered by the first item; nothing goes in.
        if (flags & (ItemHidden | ItemSeparator))
            to_index = 0;
        else
        {
            // The parent's order: before the first item with a later index.
            unsigned at = 1;
            while (at < send(target, LbGetCount, 0, 0) &&
                   ((send(target, LbGetItemData, at, 0) >> 16) & ItemIndex) < (flags & ItemIndex))
                ++at;
            to_index = int(at);
            if (send(target, LbInsertString, at, text) != at)
                failed = true;
            else
                send(target, LbSetItemData, at, data);
        }
    }
    else if (to_index < 0 || send(target, LbInsertString, unsigned(to_index), text) != unsigned(to_index))
        failed = true;
    else
    {
        // The toolbar gets the button: a separator, or the one the parent
        // offers under the item's index.
        bool have = true;
        for (unsigned i = 0; i < 20; i += 4)
            Write32(button + i, 0);
        if (flags & ItemSeparator)
            Write32(button + 8, 0x0100); // fsStyle TBSTYLE_SEP
        else
            have = ToolbarButtonInfo(toolbar, flags & ItemIndex, button, 0, 0);
        const unsigned insert[] = {toolbar, 0x415, unsigned(to_index), button}; // TB_INSERTBUTTON
        if (!have || !Api("SendMessageA", insert))
        {
            send(target, LbDeleteString, unsigned(to_index), 0);
            failed = true;
        }
        else
        {
            send(target, LbSetItemData, unsigned(to_index), data);
            if (from == CustomizeCurrent && to_index <= int(from_index))
                ++from_index;
        }
    }
    if (!failed)
    {
        // The separator of the available buttons stays.
        if (from != CustomizeAvailable || from_index != 0)
        {
            send(source, LbDeleteString, from_index, 0);
            if (from == to)
            {
                if (to_index > int(from_index))
                    --to_index;
                if (top > from_index)
                    --top;
            }
        }
        if (from == CustomizeCurrent)
        {
            const unsigned remove[] = {toolbar, 0x416, from_index, 0}; // TB_DELETEBUTTON
            Api("SendMessageA", remove);
        }
        if (from != to)
        {
            if (send(source, LbSetCurSel, from_index, 0) == ~0u)
                send(source, LbSetCurSel, from_index - 1, 0);
            const unsigned changed[] = {dialog, 0x111, from | 1u << 16, source};
            Api("SendMessageA", changed);
        }
        send(target, LbSetCurSel, unsigned(to_index) + select_next, 0);
        const unsigned changed[] = {dialog, 0x111, to | 1u << 16, target};
        Api("SendMessageA", changed);
    }
    Free(text);
    if (from == to)
        send(target, LbSetTopIndex, top, 0);
    send(target, 0xb, 1, 0);
    User::InvalidateRect(target, nullptr, true);
    if (!failed && User::IsWindow(toolbar))
        ToolbarItemNotify(toolbar, TbnToolbarChange, ~0u);
}
// WM_DRAWITEM of the two lists: the text on the window colour, or the
// highlight while the list has the focus; a button face with the image to its
// left; a frame around the selection of the list without the focus.
void Application::ToolbarCustomizeDraw(unsigned dialog, unsigned item)
{
    unsigned d[12] = {};
    if (!Read(item, d, sizeof d))
        return;
    const unsigned id = d[1], index = d[2], action = d[3], item_state = d[4], hwnd = d[5], dc = d[6];
    Gdi::RECT r{int(d[7]), int(d[8]), int(d[9]), int(d[10])};
    const unsigned data = d[11], flags = data >> 16;
    if ((id != CustomizeAvailable && id != CustomizeCurrent) || int(index) < 0)
        return;
    Toolbar *t = nullptr;
    for (auto &x : toolbars)
        if (x.object && x.customize == dialog)
            t = &x;
    if (!t)
        return;
    const unsigned object = t->object;
    const int button_width = int(Read32(object + 0x70)), button_height = int(Read32(object + 0x74));
    char text[260] = {};
    const unsigned buffer = Allocate(260);
    if (!buffer)
        return;
    if (flags & ItemSeparator)
        Win16CommonControlString(0x400, buffer, 20); // "Separator"
    else
    {
        const unsigned get[] = {hwnd, LbGetTextLen, index, 0};
        if (Api("SendMessageA", get) < 260)
        {
            const unsigned copy[] = {hwnd, LbGetText, index, buffer};
            Api("SendMessageA", copy);
        }
    }
    Read(buffer, text, sizeof text - 1);
    Free(buffer);
    auto color = [&](unsigned index) { return Api("GetSysColor", &index); };
    if (action != 4) // not ODA_FOCUS alone
    {
        const bool selected = item_state & 1;
        const bool focused = selected && User::GetFocus() == hwnd;
        const unsigned text_color = (flags & (ItemFixed | ItemHidden)) ? color(17) : focused ? color(14) : color(8);
        const unsigned old_text = Gdi::SetTextColor(dc, text_color);
        const unsigned old_back = Gdi::SetBkColor(dc, focused ? color(13) : color(5));
        Gdi::SIZE size{};
        Gdi::GetTextExtentPoint32A(dc, "W", 1, &size);
        Gdi::ExtTextOutA(dc, r.left + button_width + 2, (r.top + r.bottom - size.cy) / 2,
                         Gdi::ETO_OPAQUE | Gdi::ETO_CLIPPED, &r, text, unsigned(strlen(text)));
        if (short(data & 0xffff) >= 0)
        {
            // A button face with the image of the button, enabled unless the
            // button is hidden.
            Gdi::RECT face{r.left + 1, r.top + 1, r.left + 1 + button_width, r.top + 1 + button_height};
            if (unsigned brush = Gdi::CreateSolidBrush(color(15)))
            {
                Gdi::FillRect(dc, &face, brush);
                Gdi::DeleteObject(brush);
            }
            Gdi::RECT edge = face;
            Win98Draw::DrawEdge(dc, &edge, 5, 15);
            ToolbarMemory e(*this, *t);
            const unsigned image_value = data & 0xffff;
            ImageList *images = FindImageList(e.ImageList(object, 0, 0));
            WinImage::Image *image = images && image_value < images->count ? images->items[image_value] : nullptr;
            if (image)
            {
                const int x = face.left + (button_width - int(image->width)) / 2;
                const int y = face.top + (button_height - int(image->height)) / 2;
                WinImage::Image faded;
                if ((flags & ItemHidden) && faded.Copy(*image))
                {
                    for (unsigned n = 0; n < faded.width * faded.height; ++n)
                    {
                        const unsigned p = faded.color[n], v = ((p >> 16 & 255) + (p >> 8 & 255) + (p & 255)) / 3;
                        faded.color[n] = (p & 0xff000000) | v * 0x10101;
                    }
                    faded.Draw(dc, x, y);
                }
                else
                    image->Draw(dc, x, y);
            }
        }
        Gdi::SetTextColor(dc, old_text);
        Gdi::SetBkColor(dc, old_back);
        if (selected && !focused)
        {
            const unsigned extent_args[] = {hwnd, LbGetHorizontalExtent, 0, 0};
            const int right = int(Api("SendMessageA", extent_args)) + r.left;
            Gdi::RECT frame = r;
            if (frame.right < right)
                frame.right = right;
            if (unsigned brush = Gdi::CreateSolidBrush(color(13)))
            {
                Gdi::FrameRect(dc, &frame, brush);
                Gdi::DeleteObject(brush);
            }
        }
    }
    if (action == 4 || (item_state & 0x10))
        Gdi::DrawFocusRect(dc, &r);
}
unsigned Application::ToolbarCustomizeMessage(unsigned dialog, unsigned message, unsigned wp, unsigned lp)
{
    Toolbar *t = nullptr;
    if (message == 0x110) // WM_INITDIALOG: lParam is the toolbar
    {
        t = ToolbarOf(lp);
        if (t)
            t->customize = dialog;
    }
    else
    {
        for (auto &x : toolbars)
            if (x.object && x.customize == dialog)
                t = &x;
        // The lists ask for their item height before WM_INITDIALOG.
        if (!t)
            for (auto &x : toolbars)
                if (x.object && x.customize == ~0u)
                    t = &x;
    }
    auto end = [&](unsigned result) {
        const unsigned a[] = {dialog, result};
        Api("EndDialog", a);
    };
    if (!t)
    {
        if (message == 0x111 && (wp & 0xffff) == 2)
            end(1);
        return 0;
    }
    const unsigned toolbar = t->hwnd;
    auto selection = [&](unsigned id) {
        const unsigned a[] = {dialog, id, LbGetCurSel, 0, 0};
        return Api("SendDlgItemMessageA", a);
    };
    switch (message)
    {
    case 0x110:
    {
        if (!ToolbarCustomizeLists(dialog, toolbar, t->customize_position))
            end(0);
        const unsigned show[] = {dialog, 5};
        Api("ShowWindow", show);
        Api("UpdateWindow", &dialog);
        const unsigned focus = User::GetDlgItem(dialog, CustomizeCurrent);
        Api("SetFocus", &focus);
        return 0;
    }
    case 0x2c: // WM_MEASUREITEM: a line of text, at least the button's width and two
    {
        const unsigned id = Read32(lp + 4);
        if (id != CustomizeAvailable && id != CustomizeCurrent)
            return 0;
        int height = int(Read32(t->object + 0x70)) + 2;
        const unsigned list = User::GetDlgItem(dialog, id);
        const unsigned font_args[] = {list ? list : dialog, 0x31, 0, 0};
        const unsigned font = Api("SendMessageA", font_args);
        if (const unsigned dc = Gdi::CreateCompatibleDC(0))
        {
            const unsigned old = font ? Gdi::SelectObject(dc, font) : 0;
            Gdi::SIZE size{};
            if (Gdi::GetTextExtentPoint32A(dc, "W", 1, &size) && size.cy > height)
                height = size.cy;
            if (old)
                Gdi::SelectObject(dc, old);
            Gdi::DeleteDC(dc);
        }
        Write32(lp + 16, unsigned(height));
        return 1;
    }
    case 0x2b: // WM_DRAWITEM
        ToolbarCustomizeDraw(dialog, lp);
        return 1;
    case 0x111:
    {
        const unsigned id = wp & 0xffff, code = wp >> 16;
        switch (id)
        {
        case 1: // Add
        {
            const unsigned available = selection(CustomizeAvailable);
            unsigned current = selection(CustomizeCurrent);
            if (current == ~0u)
                current = 0;
            ToolbarCustomizeMove(dialog, CustomizeAvailable, available, CustomizeCurrent, int(current), 1);
            return 1;
        }
        case 2: // Close
            end(1);
            return 1;
        case CustomizeAvailable:
            if (code == 2) // LBN_DBLCLK
            {
                const unsigned a[] = {dialog, 0x111, 1, 0};
                Api("SendMessageA", a);
            }
            return 1;
        case CustomizeReset:
        {
            ToolbarItemNotify(toolbar, TbnReset, ~0u);
            Toolbar *again = ToolbarOf(toolbar);
            if (!again || !ToolbarCustomizeLists(dialog, toolbar, 0))
                end(1);
            return 1;
        }
        case CustomizeCurrent:
            if (code == 1) // LBN_SELCHANGE
                ToolbarCustomizeButtons(dialog);
            else if (code == 2) // LBN_DBLCLK: Remove
            {
                const unsigned a[] = {dialog, 0x111, CustomizeRemove, 0};
                Api("SendMessageA", a);
            }
            else if (code == 4 || code == 5) // LBN_SETFOCUS, LBN_KILLFOCUS: the selection's look
            {
                const unsigned rect = Allocate(16);
                const unsigned a[] = {lp, LbGetItemRect, selection(CustomizeCurrent), rect};
                if (rect && Api("SendMessageA", a) != ~0u)
                {
                    Gdi::RECT r{};
                    Read(rect, &r, sizeof r);
                    User::InvalidateRect(lp, &r, false);
                }
                if (rect)
                    Free(rect);
            }
            return 1;
        case CustomizeRemove:
            ToolbarCustomizeMove(dialog, CustomizeCurrent, selection(CustomizeCurrent), CustomizeAvailable, 0, 0);
            return 1;
        case CustomizeHelp:
            ToolbarItemNotify(toolbar, TbnCustomHelp, ~0u);
            return 1;
        case CustomizeUp:
        case CustomizeDown:
        {
            const int selected = int(selection(CustomizeCurrent));
            const unsigned count_args[] = {toolbar, 0x418, 0, 0};
            const int place = id == CustomizeUp
                                  ? ToolbarInsertPlace(toolbar, selected - 1, 0, 2)
                                  : ToolbarInsertPlace(toolbar, selected + 2, int(Api("SendMessageA", count_args)), 1);
            ToolbarCustomizeMove(dialog, CustomizeCurrent, unsigned(selected), CustomizeCurrent, place, 0);
            return 1;
        }
        default:
            return 0;
        }
    }
    default:
        return 0;
    }
}
// WrapToolbar: TBSTATE_WRAP on the buttons so that no row is wider than
// `width`. A row breaks after the last visible separator before the button
// that does not fit, or else before that button, and then at the next
// separator; a separator that ends a row counts as two rows. `rect` gets the
// size the buttons take, and `rows` their rows.
void Application::ToolbarWrap(ToolbarMemory &e, unsigned o, int width, Gdi::RECT &rect, unsigned *rows)
{
    const unsigned count = Read32(o + 0x84), buttons = Read32(o + 0x30);
    const int first_x = int(Read32(o + 0x90)), top = int(Read32(o + 0x80)), height = int(Read32(o + 0x74));
    auto state = [&](unsigned i) { return e.Read8(buttons + i * 28 + 8); };
    auto separator = [&](unsigned i) { return (e.Read8(buttons + i * 28 + 9) & 1) != 0; };
    auto wrap = [&](unsigned i, bool on) {
        e.Write8(buttons + i * 28 + 8, on ? state(i) | 32 : state(i) & ~32u);
    };
    int x = first_x, y = top, widest = int(Read32(o + 0x70));
    bool break_next = false;
    if (rows)
        *rows = 1;
    for (unsigned i = 0; i < count; ++i)
    {
        wrap(i, false);
        if (state(i) & 8)
            continue;
        x += int(Win98ToolbarLayout::Width(e, o, buttons + i * 28, 0));
        if (!separator(i) && x > width)
        {
            bool found = false;
            for (unsigned j = i; j > 0 && !(state(j) & 32); --j)
                if (separator(j) && !(state(j) & 8))
                {
                    wrap(j, true);
                    x = first_x;
                    y += int(Win98ToolbarLayout::SeparatorHeight(e, o, buttons + j * 28)) + height;
                    i = j;
                    found = true;
                    break_next = false;
                    if (rows)
                        ++*rows;
                    break;
                }
            if (!found)
            {
                unsigned j = i;
                if (i)
                {
                    do
                        --j;
                    while (j > 0 && (state(j) & 8));
                    if (state(j) & 32)
                        j = i;
                }
                wrap(j, true);
                x = first_x;
                y += height;
                i = j;
                break_next = true;
            }
            if (rows)
                ++*rows;
        }
        else if (separator(i) && break_next)
        {
            break_next = false;
            wrap(i, true);
            x = first_x;
            y += int(Win98ToolbarLayout::SeparatorHeight(e, o, buttons + i * 28)) + height;
            if (rows)
                *rows += 2;
        }
        if (!separator(i) && x > widest)
            widest = x;
    }
    rect = {0, 0, widest, y + height + top};
}
// TB_SETROWS: the narrowest width that gives the toolbar at most `rows`
// rows, growing by a quarter of a button; with `larger` the widest one that
// still gives more rows when no width gives exactly `rows`. Answers whether
// the toolbar changed; `rect` gets its size, or its client rectangle when it
// stays.
unsigned Application::ToolbarSetRows(Toolbar &t, unsigned rows, bool larger, unsigned rect)
{
    ToolbarMemory e(*this, t);
    const unsigned o = t.object, hwnd = t.hwnd;
    if (int(rows) < 1)
        rows = 1;
    const unsigned count = Read32(o + 0x84);
    if (Win98ToolbarPaint::Rows(e, o) == rows || !count)
    {
        Gdi::RECT client{};
        User::GetClientRect(hwnd, &client);
        if (rect)
            Store(rect, &client, sizeof client);
        return 0;
    }
    const int narrowest = int(Read32(o + 0x70));
    int step = narrowest / 4;
    if (step < 1)
        step = 1;
    unsigned best_rows = count + 1, got = rows + 1;
    int best_width = narrowest;
    Gdi::RECT size{};
    for (int width = narrowest; got > rows; width += step)
    {
        ToolbarWrap(e, o, width, size, &got);
        if (got < best_rows && got > rows)
        {
            best_width = width;
            best_rows = got;
        }
    }
    if (got < rows && larger)
        ToolbarWrap(e, o, best_width, size, nullptr);
    Win98Toolbar::Dirty(e, o);
    LayoutToolbar(t);
    const unsigned position[] = {hwnd, 0, 0, 0, unsigned(size.right), unsigned(size.bottom), 0x16};
    PositionApi("SetWindowPos", position);
    User::InvalidateRect(hwnd, nullptr, true);
    if (rect)
        Store(rect, &size, sizeof size);
    return 1;
}
// TB_GETBUTTONTEXT: the text of the button with the command, its length
// without the zero, or minus one for no such button or no text.
unsigned Application::ToolbarButtonText(Toolbar &t, unsigned command, unsigned text)
{
    ToolbarMemory e(*this, t);
    const unsigned o = t.object, index = Win98Toolbar::CommandIndex(e, o, command);
    if (int(index) < 0)
        return ~0u;
    const unsigned wide = Win98ToolbarPaint::Text(e, o, Read32(o + 0x30) + index * 28);
    if (!wide)
        return ~0u;
    const unsigned scratch = Allocate(1024);
    if (!scratch)
        return ~0u;
    unsigned a[] = {0, 0, wide, ~0u, scratch, 1024, 0, 0};
    const unsigned converted = NlsApi(811, a); // WideCharToMultiByte
    char bytes[1024] = {};
    if (converted)
        Read(scratch, bytes, sizeof bytes - 1);
    Free(scratch);
    if (!converted)
        return ~0u;
    const unsigned length = unsigned(strlen(bytes));
    if (text)
        Store(text, bytes, length + 1);
    return length;
}
// TB_ADDSTRING: strings into the toolbar's own list, which TBBUTTON.iString
// indexes (+58h the list of wide strings, +5Ch their number). Without an
// instance lParam points to the strings, each ended by a zero and the list by
// a second one; with one it is string LOWORD(lParam) of that module, and its
// first character separates the strings. Answers the index of the first new
// string, or minus one.
unsigned Application::ToolbarAddStrings(Toolbar &t, unsigned instance, unsigned strings)
{
    ToolbarMemory e(*this, t);
    const unsigned o = t.object;
    unsigned loaded = 0, source = strings;
    if (instance)
    {
        loaded = Allocate(0x404);
        const unsigned a[] = {instance, strings & 0xffff, loaded, 0x400};
        const unsigned length = loaded ? Api("LoadStringA", a) : 0;
        if (!length)
        {
            if (loaded)
                Free(loaded);
            return ~0u;
        }
        char text[0x404] = {};
        Read(loaded, text, length + 1);
        const char separator = text[0];
        for (unsigned i = 1; i < length; ++i)
            text[i - 1] = text[i] == separator ? 0 : text[i];
        text[length - 1] = text[length] = 0;
        Store(loaded, text, length + 1);
        source = loaded;
    }
    unsigned count = 0;
    if (source)
        for (unsigned at = source, guard = 0; guard < 0x10000; ++guard)
        {
            unsigned char c = 0;
            if (!Read(at, &c, 1) || !c)
                break;
            while (Read(at, &c, 1) && c)
                ++at;
            ++at;
            ++count;
        }
    const unsigned old = Read32(o + 0x5c), table = Read32(o + 0x58);
    unsigned answer = ~0u;
    if (count)
        if (const unsigned grown = e.AllocateLocal(0x40, (old + count) * 4))
        {
            for (unsigned i = 0; i < old; ++i)
                Write32(grown + i * 4, Read32(table + i * 4));
            unsigned at = source, added = 0;
            for (; added < count; ++added)
            {
                const unsigned wide = e.AnsiToWide(Read32(o + 0x14), at);
                if (!wide)
                    break;
                Write32(grown + (old + added) * 4, wide);
                unsigned char c = 0;
                while (Read(at, &c, 1) && c)
                    ++at;
                ++at;
            }
            if (table)
                e.FreeLocal(table);
            Write32(o + 0x58, grown);
            Write32(o + 0x5c, old + added);
            answer = added ? old : ~0u;
            Win98Toolbar::Dirty(e, o);
            LayoutToolbar(t);
            User::InvalidateRect(t.hwnd, nullptr, true);
        }
    if (loaded)
        Free(loaded);
    return answer;
}
} // namespace X86
