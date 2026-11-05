// SPDX-License-Identifier: GPL-2.0-or-later
// The multiple document interface of USER32. It includes MDICLIENT, DefFrameProc,
// DefMDIChildProc and TranslateMDISysAccel.
#include "user.h"
#include "x86_app.h"
#include <stdio.h>
#include <string.h>
namespace X86 {
namespace {
// Window styles and the two the MDI adds to every child.
constexpr unsigned StyleChild = 0x40000000, StyleVisible = 0x10000000;
constexpr unsigned StyleMinimize = 0x20000000, StyleMaximize = 0x01000000;
constexpr unsigned StyleClipSiblings = 0x04000000, StyleSysMenu = 0x00080000;
constexpr unsigned StyleCaption = 0x00C00000, StyleThickFrame = 0x00040000;
constexpr unsigned StyleMinimizeBox = 0x00020000, StyleMaximizeBox = 0x00010000;
constexpr unsigned StyleDisabled = 0x08000000;
// Ordinary MDI children retain caller bits except WS_POPUP and gain the frame.
constexpr unsigned ChildStyles = StyleChild | StyleVisible | StyleClipSiblings | StyleCaption |
                                 StyleSysMenu | StyleThickFrame | StyleMinimizeBox | StyleMaximizeBox;
// The messages segment 17 answers.
constexpr unsigned WmSize = 0x0005, WmSetFocus = 0x0007, WmSetText = 0x000C;
constexpr unsigned WmClose = 0x0010, WmChildActivate = 0x0022, WmNcActivate = 0x0086;
constexpr unsigned WmGetMinMaxInfo = 0x0024;
constexpr unsigned WmCommand = 0x0111, WmSysCommand = 0x0112, WmMenuChar = 0x0120;
constexpr unsigned WmMdiCreate = 0x0220, WmMdiDestroy = 0x0221, WmMdiActivate = 0x0222;
constexpr unsigned WmMdiRestore = 0x0223, WmMdiNext = 0x0224, WmMdiMaximize = 0x0225;
constexpr unsigned WmMdiTile = 0x0226, WmMdiCascade = 0x0227, WmMdiIconArrange = 0x0228;
constexpr unsigned WmMdiGetActive = 0x0229, WmMdiSetMenu = 0x0230, WmMdiRefreshMenu = 0x0234;
// System commands handled by the MDI client and child windows.
constexpr unsigned ScRestore = 0xF120, ScNextWindow = 0xF040, ScPrevWindow = 0xF050;
constexpr unsigned ScClose = 0xF060;
// the tenth entry of the window list is the "More Windows..." item,
// so at most nine children ever stand in the menu.
constexpr unsigned MenuChildLimit = 9;
// Virtual keys TranslateMDISysAccel reads.
constexpr unsigned VkTab = 0x09, VkF4 = 0x73, VkF6 = 0x75;
constexpr unsigned VkShift = 0x10, VkControl = 0x11, VkMenu = 0x12;
} // namespace

// USER registers MDICLIENT with 22 window extra bytes. The original
// keeps its record in them; NEO98 keeps the same fields in a table beside the
// window and leaves the 22 bytes to the program, because GetWindowWord on an
// MDI client is not part of any documented contract and a program that writes
// there would otherwise destroy the client. The class still reports 22 bytes.
Application::MdiClient *Application::MdiRecord(unsigned hwnd, bool create)
{
    for (auto &record : mdi_clients)
        if (record.hwnd == hwnd && hwnd) return &record;
    if (!create) return nullptr;
    for (auto &record : mdi_clients)
        if (!record.hwnd)
        {
            record = MdiClient{};
            record.hwnd = hwnd;
            return &record;
        }
    return nullptr;
}

void Application::MdiChildGeometry(unsigned hwnd, unsigned *create)
{
    auto *record = MdiRecord(hwnd, false);
    if (!record) return;
    // Default MDI child layout uses steps of SM_CXSIZE+SM_CXFRAME
    // horizontally and SM_CYSIZE+SM_CYFRAME vertically. The client height
    // divided by three vertical steps determines the cascade count. Subtract
    // the full cascade span from the client size and choose the position from
    // the cumulative creation count. That count wraps after 0x7FFE and does
    // not decrease on close. Both CW_USEDEFAULT and zero request default
    // dimensions, as required by File Manager.
    Gdi::RECT area = {};
    User::GetClientRect(hwnd, &area);
    const unsigned size_x[] = {30}, frame_x[] = {32}, size_y[] = {31}, frame_y[] = {33};
    const int step_x = int(Api("GetSystemMetrics", size_x) + Api("GetSystemMetrics", frame_x));
    const int step_y = int(Api("GetSystemMetrics", size_y) + Api("GetSystemMetrics", frame_y));
    const int span_x = area.right - area.left, span_y = area.bottom - area.top;
    // Use at least one cascade step, including for an empty MDI client, to
    // avoid division by zero.
    int slots = step_y > 0 && span_y > 0 ? span_y / (3 * step_y) : 0;
    if (slots < 0) slots = 0;
    const int stair = int(record->created) % (slots + 1);
    const int default_x = area.left + stair * step_x, default_y = area.top + stair * step_y;
    const int default_width = span_x - slots * step_x, default_height = span_y - slots * step_y;
    if (create[5] == 0x80000000u || !create[5]) create[5] = unsigned(default_width);
    if (create[6] == 0x80000000u || !create[6]) create[6] = unsigned(default_height);
    if (create[3] == 0x80000000u)
    {
        create[3] = unsigned(default_x);
        create[4] = unsigned(default_y);
    }
}

// The children of one client in the order Windows 98 walks them: by control id.
// The original asks for idFirstChild, idFirstChild+1 and so on, and
// CreateWindow hands out those ids in creation order, so this is the
// creation order and not the Z order that WindowSnapshot answers.
unsigned Application::MdiChildren(unsigned client, unsigned *out, unsigned capacity)
{
    unsigned windows[User::WindowCapacity], count = 0, taken = 0;
    if (!User::WindowSnapshot(client, windows, User::WindowCapacity, count)) return 0;
    const auto *record = MdiRecord(client, false);
    for (unsigned i = 0; i < count && taken < capacity; ++i)
        // WindowSnapshot includes descendants; MDI only enumerates the
        // immediate frames, never their views, buttons or edit controls.
        if (User::IsWindow(windows[i]) && User::Parent(windows[i]) == client &&
            (!record || windows[i] != record->removing_child)) out[taken++] = windows[i];
    for (unsigned i = 1; i < taken; ++i)
    {
        const unsigned window = out[i], id = User::ControlID(window);
        unsigned j = i;
        while (j && User::ControlID(out[j - 1]) > id) { out[j] = out[j - 1]; --j; }
        out[j] = window;
    }
    return taken;
}

// rebuilds the tail of the window menu: a separator, then up to nine
// children as "&<n> <title>", then "More Windows..." when a tenth exists. The
// ids start at the idFirstChild of the CLIENTCREATESTRUCT, so DefFrameProc can
// recognize them by their number alone.
void Application::MdiRefreshMenu(MdiClient &record)
{
    if (!record.menu || !User::IsMenu(record.menu)) { record.listed = 0; return; }
    // Take the old list out again, from the back, so the indices hold.
    for (unsigned i = record.listed; i--;)
        User::RemoveMenu(record.menu, record.first_id + i, false, false);
    if (record.separator)
    {
        const unsigned count = User::MenuItemCount(record.menu);
        if (count) User::RemoveMenu(record.menu, count - 1, true, false);
        record.separator = false;
    }
    record.listed = 0;
    unsigned children[User::WindowCapacity];
    const unsigned count = MdiChildren(record.hwnd, children, User::WindowCapacity);
    if (!count) return;
    // puts the separator in before the first entry.
    User::AppendMenu(record.menu, 0x800, 0, nullptr, 0);
    record.separator = true;
    const unsigned listed = count > MenuChildLimit ? MenuChildLimit : count;
    for (unsigned i = 0; i < listed; ++i)
    {
        char title[128] = {}, label[160];
        User::GetWindowText(children[i], title, sizeof title);
        snprintf(label, sizeof label, "&%u %s", i + 1, title);
        User::AppendMenu(record.menu, 0, record.first_id + i, label, 0);
    }
    record.listed = listed;
    if (count > MenuChildLimit)
    {
        // Windows 98 answers the tenth id with the Select Window dialog.
        User::AppendMenu(record.menu, 0, record.first_id + MenuChildLimit, "&More Windows...", 0);
        record.listed = MenuChildLimit + 1;
    }
    User::DrawMenuBar(User::Parent(record.hwnd));
}

// composes the frame title. While a child is maximized the frame shows
// "Frame - [Child]"; as soon as the child leaves that state the frame gets its
// own title back. The original keeps the frame's own text beside the record,
// and so does this.
void Application::MdiComposeTitle(MdiClient &record)
{
    const unsigned frame = User::Parent(record.hwnd);
    if (!frame || !User::IsWindow(frame)) return;
    char child[128] = {}, title[320];
    if (record.maximized && User::IsWindow(record.maximized))
    {
        User::GetWindowText(record.maximized, child, sizeof child);
        snprintf(title, sizeof title, "%s - [%s]", record.frame_title, child);
    }
    else
        snprintf(title, sizeof title, "%s", record.frame_title);
    record.composing = true;
    User::SetWindowText(frame, title);
    record.composing = false;
    MdiComposeMenu(record);
}

void Application::MdiComposeMenu(MdiClient &record)
{
    if (record.maximized && User::CreationPending(record.maximized)) return;
    if (record.composing_menu) return;
    record.composing_menu = true;
    struct MenuComposition {
        MdiClient &record; unsigned client;
        ~MenuComposition() { if (record.hwnd == client) record.composing_menu = false; }
    } composition{record, record.hwnd};
    const unsigned frame = User::Parent(record.hwnd), menu = User::GetMenu(frame);
    const unsigned child = User::IsWindow(record.maximized) &&
        ((User::Style(record.maximized) & StyleSysMenu) || record.decorated_child == record.maximized)
        ? record.maximized : 0;
    if (record.decorated_menu == menu && record.decorated_child == child) return;
    // Menu composition precedes the frame's SetWindowPos. Preserve its
    // old client rectangle until WM_NCCALCSIZE computes the new menu height.
    const unsigned previous_child = record.decorated_child;
    if (User::IsWindow(previous_child))
        User::SetStyleWords(previous_child, User::Style(previous_child) | StyleSysMenu, false);
    if (User::IsMenu(record.decorated_menu))
        for (unsigned count = User::MenuItemCount(record.decorated_menu); count; --count)
        {
            Win98Menu::Info item{}; item.mask = 2 | 4 | 0x80;
            if (!User::MenuItemInfo(record.decorated_menu, count - 1, 0x400, item, 0)) continue;
            if ((item.bitmap == 1 && item.submenu == record.decorated_system) ||
                (item.bitmap == 3 && item.id == 0xf020) ||
                (item.bitmap == 2 && item.id == ScRestore) ||
                (item.bitmap == 5 && item.id == ScClose))
                User::RemoveMenu(record.decorated_menu, count - 1, true, false, false);
        }
    record.decorated_menu = record.decorated_child = record.decorated_system = 0;
    if (User::IsWindow(previous_child))
    {
        // USER restores WS_SYSMENU and recalculates the old child's
        // frame when its menu buttons leave the frame menu.
        unsigned position[1][7] = {{previous_child, 0, 0, 0, 0, 0, 0x37}};
        PositionWindows(1, position);
        if (!User::IsWindow(composition.client) || record.hwnd != composition.client) return;
    }
    if (child && User::IsMenu(menu))
    {
        const unsigned arguments[] = {child, 0};
        const unsigned system = Api("GetSystemMenu", arguments);
        if (!system) return;
        Win98Menu::Info icon{}; icon.mask = 2 | 4 | 0x80; icon.submenu = system; icon.bitmap = 1;
        if (!User::MenuItemInfo(menu, 0, 0x400, icon, 2, false)) return;
        record.decorated_menu = menu; record.decorated_child = child; record.decorated_system = system;
        const unsigned commands[] = {0xf020, ScRestore, ScClose}, bitmaps[] = {3, 2, 5};
        for (unsigned i = 0; i < 3; ++i)
        {
            Win98Menu::Info item{}; item.mask = 1 | 2 | 0x80 | 0x100;
            item.id = commands[i]; item.bitmap = bitmaps[i]; item.type = i ? 0 : 0x4000;
            const unsigned state = User::MenuState(system, commands[i], 0);
            item.state = i == 1 ? 0 : state == ~0u ? 3 : state & 3;
            User::MenuItemInfo(menu, 0xffff, 0x400, item, 2, false);
        }
        // USER removes the child's own system-menu bit after
        // moving its buttons to the frame, then recalculates its frame.
        User::SetStyleWords(child, User::Style(child) & ~StyleSysMenu, false);
        unsigned position[1][7] = {{child, 0, 0, 0, 0, 0, 0x37}};
        PositionWindows(1, position);
    }
    if (User::IsWindow(frame)) User::DrawMenuBar(frame, false);
}

// Activating a child tells the old and the new one about the change,
// puts the new one on top and moves the check mark in the window menu.
void Application::MdiActivate(MdiClient &record, unsigned child)
{
    if (child && (!User::IsWindow(child) || User::Parent(child) != record.hwnd)) return;
    const unsigned old = record.active;
    if (old == child) return;
    if (old && User::IsWindow(old))
    {
        Send(old, WmNcActivate, 0, 0);
        Send(old, WmMdiActivate, old, child);
    }
    // USER restores the maximized child when activation is cleared.
    // Its WM_SIZE removes the menu decoration before the active field clears.
    if (!child && User::IsWindow(record.maximized))
    {
        const unsigned client = record.hwnd;
        ShowWindow(record.maximized, 1);
        if (!User::IsWindow(client) || record.hwnd != client) return;
    }
    record.active = child;
    if (child)
    {
        if (record.maximized != child && User::IsWindow(record.maximized) &&
            (User::Style(record.maximized) & StyleMaximize))
        {
            const unsigned client = record.hwnd;
            ShowWindow(child, 3);
            if (!User::IsWindow(client) || !User::IsWindow(child)) return;
        }
        const unsigned position[] = {child, 0, 0, 0, 0, 0, 0x13}; // SWP_NOMOVE|NOSIZE|NOACTIVATE
        Api("SetWindowPos", position);
        Send(child, WmNcActivate, 1, 0);
        const unsigned client = record.hwnd, frame = User::Parent(client);
        if (User::IsWindow(frame) && User::ThreadActivation(User::WindowThread(frame)).active == frame)
            SetFocus(client); // MDICLIENT forwards focus through the child's procedure.
        if (!User::IsWindow(client) || !User::IsWindow(child)) return;
        Send(child, WmMdiActivate, old, child);
    }
    if (record.menu && User::IsMenu(record.menu))
        for (unsigned i = 0; i < record.listed && i < MenuChildLimit; ++i)
        {
            unsigned children[User::WindowCapacity];
            const unsigned count = MdiChildren(record.hwnd, children, User::WindowCapacity);
            User::SetMenuFlags(record.menu, record.first_id + i, i < count && children[i] == child ? 8u : 0u,
                               false, 8);
        }
    MdiComposeTitle(record);
}

// The MDICLIENT window procedure of segment 17.
unsigned Application::MdiClientMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp)
{
    MdiClient *record = MdiRecord(hwnd, message == 0x0001);
    if (!record) return DefaultWindowMessage(hwnd, message, wp, lp);
    switch (message)
    {
    case 0x0001: // WM_CREATE reads the CLIENTCREATESTRUCT out of lpCreateParams
    {
        unsigned create[12] = {};
        unsigned parameters = 0, client[2] = {};
        if (Read(lp, create, sizeof create)) parameters = create[0];
        if (parameters && Read(parameters, client, sizeof client))
        {
            record->menu = client[0];
            record->first_id = client[1];
        }
        const unsigned frame = User::Parent(hwnd);
        if (frame) User::GetWindowText(frame, record->frame_title, sizeof record->frame_title);
        return 0;
    }
    case 0x0002: // WM_DESTROY
        *record = MdiClient{};
        return 0;
    case WmSetFocus:
        // USER forwards focus to the active child unless it
        // is minimized. Use the focus API so the child's procedure runs.
        if (User::IsWindow(record->active) && !(User::Style(record->active) & StyleMinimize))
            SetFocus(record->active);
        return 0;
    case WmSize:
        // A maximized child's client covers the MDI client. Its outer
        // rectangle still includes its caption and borders, even while
        // the MDI client is hidden.
        // USER reads the active child (+5Ch), not the menu's
        // maximized record (+58h), then tests the child's WS_MAXIMIZE bit.
        if (User::IsWindow(record->active) && (User::Style(record->active) & StyleMaximize))
        {
            const unsigned child = record->active;
            Gdi::RECT area = {0, 0, int(lp & 65535), int(lp >> 16)};
            User::AdjustWindowRect(&area, User::Style(child), false, User::ExtendedStyle(child));
            const unsigned size[] = {child, 0, unsigned(area.left), unsigned(area.top),
                                    unsigned(area.right - area.left), unsigned(area.bottom - area.top), 0x14};
            Api("SetWindowPos", size);
        }
        return 0;
    case WmMdiCreate:
    {
        // MDICREATESTRUCT: class, title, instance, x, y, cx, cy, style, param.
        unsigned create[9] = {};
        if (!Read(lp, create, sizeof create)) { last_error = 87; return 0; }
        if (!create[0]) return 0;
        unsigned children[User::WindowCapacity];
        const unsigned count = MdiChildren(hwnd, children, User::WindowCapacity);
        // hands the new child the next free id above idFirstChild.
        if ((User::Style(hwnd) & 1) && (create[7] & 0x80000000u)) return 0;
        const unsigned style = User::Style(hwnd) & 1
            ? create[7] | StyleChild | StyleClipSiblings
            : (create[7] & ~0x80000000u) | ChildStyles;
        MdiChildGeometry(hwnd, create);
        // Use ordinary window creation so the MDI child receives WM_NCCREATE,
        // WM_NCCALCSIZE and WM_CREATE. Its creation parameter is the
        // MDICREATESTRUCT pointer itself; the application reads its value
        // through lpCreateParams->lParam.
        const unsigned arguments[12] = {0x40,          create[0],  create[1],
                                        style,      create[3],  create[4],
                                        create[5],  create[6],  hwnd,
                                        record->first_id + count, create[2], lp};
        const unsigned child = CreateWindow(arguments, true);
        if (!child) return 0;
        return child;
    }
    case WmMdiDestroy:
    {
        if (!User::IsWindow(wp) || User::Parent(wp) != hwnd) return 0;
        unsigned children[User::WindowCapacity];
        const unsigned count = MdiChildren(hwnd, children, User::WindowCapacity);
        unsigned next = 0;
        for (unsigned i = 0; i < count; ++i)
            if (children[i] != wp) { next = children[i]; break; }
        struct RemovingChild {
            MdiClient &record; unsigned client, previous;
            ~RemovingChild() { if (record.hwnd == client) record.removing_child = previous; }
        } removing{*record, hwnd, record->removing_child};
        record->removing_child = wp;
        // USER changes children through the positioning path before
        // destroying the old one. Keep its maximized state until the new
        // child's activation has transferred it and restored the old child.
        if (record->active == wp && next)
        {
            const bool hide_client = record->maximized != 0;
            // Windows 98 uses USER to clear visibility without sending
            // WM_SHOWWINDOW; the final ShowWindow sends the show normally.
            if (hide_client) User::SetStyleWords(hwnd, User::Style(hwnd) & ~StyleVisible, false);
            // defers both positions as one operation: the
            // successor comes to the top and the closing child goes below it.
            unsigned positions[2][7] = {{next, 0, 0, 0, 0, 0, 3},
                                       {wp, 1, 0, 0, 0, 0, 0x13}};
            PositionWindows(2, positions);
            if (hide_client && User::IsWindow(hwnd)) ShowWindow(hwnd, 5);
        }
        else if (record->active == wp)
        {
            // hides the last active child before removing its menu
            // and clearing the client's active/maximized records.
            ShowWindow(wp, 0);
            record = MdiRecord(hwnd, false);
            if (!record) return 0;
            if (record->maximized == wp)
            {
                record->maximized = 0;
                MdiComposeTitle(*record);
                const unsigned frame = User::Parent(hwnd);
                if (User::IsWindow(frame))
                {
                    unsigned position[1][7] = {{frame, 0, 0, 0, 0, 0, 0x37}};
                    PositionWindows(1, position);
                }
                record = MdiRecord(hwnd, false);
                if (!record) return 0;
            }
            MdiActivate(*record, 0);
        }
        // USER refreshes the window list before the destroy hook.
        if (User::IsWindow(hwnd)) Send(hwnd, WmMdiRefreshMenu, 0, 0);
        // WM_MDIDESTROY runs the complete USER destruction sequence: WM_DESTROY
        // and WM_NCDESTROY reach the frame and its views, so MFC's document
        // view list does not outlive its windows (3DMark2000).
        CbtDestroyWindow(wp, false);
        record = MdiRecord(hwnd, false);
        if (!record || !User::IsWindow(hwnd) || User::IsWindow(wp)) return 0;
        if (record->maximized == wp) record->maximized = 0;
        if (record->active == wp) record->active = 0;
        MdiRefreshMenu(*record);
        MdiActivate(*record, next);
        return 0;
    }
    case WmMdiActivate:
        if (User::IsWindow(wp) && User::Parent(wp) == hwnd)
        {
            // The requested child receives WM_CHILDACTIVATE during the
            // positioning operation; its default procedure changes state.
            const unsigned position[] = {wp, 0, 0, 0, 0, 0, 3};
            Api("SetWindowPos", position);
        }
        return 0;
    case WmMdiGetActive:
        if (lp)
        {
            // USER tests the maximized record independently of the
            // active window's validity.
            const unsigned maximized = record->maximized ? 1u : 0u;
            Store(lp, &maximized, 4);
        }
        // USER validates the stored window before publishing its HWND.
        return User::IsWindow(record->active) ? record->active : 0;
    case WmMdiNext:
    {
        unsigned children[User::WindowCapacity];
        const unsigned count = MdiChildren(hwnd, children, User::WindowCapacity);
        if (!count) return 0;
        unsigned at = 0;
        const unsigned from = wp ? wp : record->active;
        for (unsigned i = 0; i < count; ++i)
            if (children[i] == from) { at = i; break; }
        const unsigned next = lp ? children[(at + count - 1) % count] : children[(at + 1) % count];
        MdiActivate(*record, next);
        return 0;
    }
    case WmMdiMaximize:
        if (!User::IsWindow(wp) || User::Parent(wp) != hwnd) return 0;
        // WM_SIZE commits the maximized child and updates frame/menu state.
        // Publishing it early makes that handler miss the transition.
        ShowWindow(wp, 3); // SW_MAXIMIZE
        record = MdiRecord(hwnd, false);
        if (!record || !User::IsWindow(wp)) return 0;
        MdiActivate(*record, wp);
        MdiComposeTitle(*record);
        return 0;
    case WmMdiRestore:
        if (!User::IsWindow(wp)) return 0;
        ShowWindow(wp, 9); // SW_RESTORE
        record = MdiRecord(hwnd, false);
        if (!record) return 0;
        MdiComposeTitle(*record);
        return 0;
    case WmMdiTile:
    {
        const unsigned arguments[] = {hwnd, wp, 0, 0, 0};
        return ArrangeApi("TileWindows", arguments);
    }
    case WmMdiCascade:
    {
        const unsigned arguments[] = {hwnd, wp, 0, 0, 0};
        return ArrangeApi("CascadeWindows", arguments);
    }
    case WmMdiIconArrange:
        return ArrangeIcons(hwnd);
    case WmMdiSetMenu:
    {
        const unsigned frame = User::Parent(hwnd);
        const unsigned old = User::GetMenu(frame);
        if ((wp && !User::IsMenu(wp)) || (lp && !User::IsMenu(lp))) return 0;
        if (lp && lp != record->menu)
        {
            // Remove the generated child list from the old Window menu.
            for (unsigned i = record->listed; i--;)
                User::RemoveMenu(record->menu, record->first_id + i, false, false);
            if (record->separator && User::IsMenu(record->menu))
            {
                const unsigned count = User::MenuItemCount(record->menu);
                if (count) User::RemoveMenu(record->menu, count - 1, true, false);
            }
            record->menu = lp; record->listed = 0; record->separator = false;
        }
        if (frame && wp) User::SetMenu(frame, wp);
        MdiRefreshMenu(*record);
        MdiComposeMenu(*record);
        return old;
    }
    case WmMdiRefreshMenu:
        MdiRefreshMenu(*record);
        return record->menu;
    default:
        break;
    }
    return DefaultWindowMessage(hwnd, message, wp, lp);
}

// USER. DefFrameProc answers seven messages itself and hands the rest
// to DefWindowProc; the dispatch is the whole list.
unsigned Application::DefFrameMessage(unsigned frame, unsigned client, unsigned message, unsigned wp,
                                      unsigned lp)
{
    MdiClient *record = client && User::IsWindow(client) ? MdiRecord(client, false) : nullptr;
    if (record) switch (message)
    {
    case WmSize:
        // the client covers the whole client area of the frame. With
        // wParam SIZE_MINIMIZED (1) the original measures the frame instead of
        // taking the size out of lParam.
        if (wp != 1)
        {
            const unsigned position[] = {client, 0, 0, 0, lp & 65535, lp >> 16, 0x14};
            Api("SetWindowPos", position);
        }
        else
        {
            Gdi::RECT area = {};
            User::GetClientRect(frame, &area);
            const unsigned position[] = {client, 0, 0, 0, unsigned(area.right - area.left),
                                         unsigned(area.bottom - area.top), 0x14};
            Api("SetWindowPos", position);
        }
        return 0;
    case WmSetFocus:            //
        SetFocus(client);
        return 0;
    case WmSetText:
    {
        // goes to the title composition, so the frame's own text is
        // remembered and the caption is rebuilt from it.
        if (!record->composing)
        {
            const char *text = lp ? ReadString(lp) : "";
            if (state == Failed) return 0;
            unsigned n = 0;
            while (text && text[n] && n < sizeof record->frame_title - 1)
            { record->frame_title[n] = text[n]; ++n; }
            record->frame_title[n] = 0;
            MdiComposeTitle(*record);
            // never reaches DefWindowProc and answers zero.
            return 0;
        }
        break;
    }
    case WmNcActivate:          // forwards the state to the client
        Send(client, WmNcActivate, wp, lp);
        break;
    case WmCommand:
        if (User::IsWindow(record->maximized) && !lp &&
            ((wp & 0xffff) == 0xf020 || (wp & 0xffff) == ScRestore || (wp & 0xffff) == ScClose))
            return Send(record->maximized, WmSysCommand, wp & 0xffff, 0);
        // an id inside the window list activates that child.
        if (record->listed && wp >= record->first_id && wp < record->first_id + record->listed)
        {
            const unsigned index = wp - record->first_id;
            if (index == MenuChildLimit)
            {
                // opens the Select Window dialog, USER resource 9.
                // NEO98 has no native runner for it, so the tenth entry keeps
                // the list unchanged and reports this limitation.
                //
                last_error = 120;
                return 0;
            }
            unsigned children[User::WindowCapacity];
            const unsigned count = MdiChildren(client, children, User::WindowCapacity);
            if (index >= count) return 0;
            Send(client, WmMdiActivate, children[index], 0);
            // a minimized child is restored on the way.
            if (User::IsWindow(children[index]) && (User::Style(children[index]) & StyleMinimize))
                Send(children[index], WmSysCommand, ScRestore, 0);
            return 0;
        }
        break;
    default:
        break;
    }
    return DefaultWindowMessage(frame, message, wp, lp);
}

// USER. A child that does not answer a message itself lets the frame
// see it, so the frame's menu and system commands keep working.
unsigned Application::DefMdiChildMessage(unsigned child, unsigned message, unsigned wp, unsigned lp)
{
    const unsigned client = User::Parent(child);
    MdiClient *record = client ? MdiRecord(client, false) : nullptr;
    switch (message)
    {
    case WmChildActivate:               //
        if (record) MdiActivate(*record, child);
        return 0;
    case WmSetFocus:                    //
        if (record && record->active != child) MdiActivate(*record, child);
        break;
    case WmGetMinMaxInfo:
    {
        // Windows 98 writes only ptMaxSize and ptMaxPosition, out of the client
        // area of the MDI client grown by the child's own frame.
        if (!record) break;
        Gdi::RECT area = {};
        User::GetClientRect(client, &area);
        User::AdjustWindowRect(&area, User::Style(child), false, User::ExtendedStyle(child));
        unsigned info[10] = {};
        if (!Read(lp, info, sizeof info)) break;
        info[2] = unsigned(area.right - area.left);
        info[3] = unsigned(area.bottom - area.top);
        info[4] = unsigned(area.left);
        info[5] = unsigned(area.top);
        Store(lp, info, sizeof info);
        return 0;
    }
    case WmSetText:
    {
        const unsigned result = DefaultWindowMessage(child, message, wp, lp);
        // the frame title follows the child's while it is maximized.
        if (record) { MdiRefreshMenu(*record); MdiComposeTitle(*record); }
        return result;
    }
    case WmClose:
        // turns the close of a child into WM_MDIDESTROY, so the client
        // picks the next child and cleans the menu.
        if (record) { Send(client, WmMdiDestroy, child, 0); return 0; }
        break;
    case WmMenuChar:
        // a letter the child's system menu does not know goes to the
        // frame, the parent of the client, as a posted WM_SYSCOMMAND
        // SC_KEYMENU with the letter, so ALT+F in a child opens File of the
        // frame; the answer 00010000h closes the child's menu mode silently.
        if (record)
        {
            const unsigned frame = User::Parent(client);
            if (frame && User::IsWindow(frame)) User::PostMessage(frame, WmSysCommand, 0xf100, wp & 0xffff);
        }
        return 0x00010000;
    case WmSysCommand:
        if ((wp & 0xfff0) == ScNextWindow || (wp & 0xfff0) == ScPrevWindow)
        {
            if (record) Send(client, WmMdiNext, child, (wp & 0xfff0) == ScPrevWindow ? 1u : 0u);
            return 0;
        }
        break;
    case WmSize:
        // the maximized state of a child is the state of the frame
        // caption, so every size change rebuilds the title.
        if (record)
        {
            const unsigned previous_maximized = record->maximized;
            if (wp == 2)
            {
                const unsigned old = record->maximized;
                record->maximized = child;
                if (old != child && User::IsWindow(old) && (User::Style(old) & StyleMaximize))
                {
                    // Restore the previous maximized child without activating
                    // it again. USER uses its internal SW_NORMALNA command.
                    Send(old, 0xb, 0, 0);
                    if (User::IsWindow(old) && !CbtEvent(1, old, 0xcc) && User::IsWindow(old))
                    {
                        const unsigned style = User::Style(old);
                        ShowTransition(old, 0xcc, style, (style & StyleVisible) != 0);
                    }
                    if (User::IsWindow(old)) Send(old, 0xb, 1, 0);
                }
                record = MdiRecord(client, false);
                if (!record || !User::IsWindow(child)) return 0;
            }
            else if (record->maximized == child) record->maximized = 0;
            MdiComposeTitle(*record);
            if (previous_maximized != record->maximized)
            {
                // Windows 98 call the frame-title helper with flag 1:
                // its frame change is observable even without a menu.
                const unsigned frame = User::Parent(client);
                if (User::IsWindow(frame))
                {
                    unsigned position[1][7] = {{frame, 0, 0, 0, 0, 0, 0x37}};
                    PositionWindows(1, position);
                }
            }
            if (wp == 1 && User::IsWindow(client))
            {
                // minimization activates the first visible,
                // unowned child in Z order when the client is in the active
                // frame. This may be the same child, but the message remains.
                const unsigned active = User::ThreadActivation(User::WindowThread(client)).active;
                unsigned ancestor = User::Parent(client);
                for (unsigned n = 0; ancestor && ancestor != active && n < User::WindowCapacity; ++n)
                    ancestor = User::Parent(ancestor);
                if (active && ancestor == active)
                {
                    unsigned children[User::WindowCapacity], count = 0;
                    User::WindowSnapshot(client, children, User::WindowCapacity, count);
                    for (unsigned i = 0; i < count; ++i)
                        if (User::IsVisible(children[i]) && !User::Owner(children[i]))
                        {
                            Send(children[i], WmChildActivate, 0, 0);
                            break;
                        }
                }
            }
        }
        break;
    default:
        break;
    }
    return DefaultWindowMessage(child, message, wp, lp);
}

unsigned Application::MdiApi(ApiName name, const unsigned *a)
{
    switch (name.id)
    {
    API_CASE("DefFrameProcA") return DefFrameMessage(a[0], a[1], a[2], a[3], a[4]);
    API_CASE("DefMDIChildProcA") return DefMdiChildMessage(a[0], a[1], a[2], a[3]);
    // USER. Only a key press reaches the tests, and only with CTRL
    // held: F6 and TAB step through the children, SHIFT reverses the
    // direction, and F4 closes the active child. Each one becomes one
    // WM_SYSCOMMAND on the active child.
    API_CASE("TranslateMDISysAccel")
    {
        if (!User::IsWindow(a[0])) return 0;
        User::Message message = {};
        if (!Read(a[1], &message, sizeof message)) { last_error = 87; return 0; }
        if (message.message != 0x0100 && message.message != 0x0104) return 0;
        MdiClient *record = MdiRecord(a[0], false);
        const unsigned child = record ? record->active : 0;
        // There must be an active child and it must not be
        // disabled.
        if (!child || !User::IsWindow(child) || (User::Style(child) & StyleDisabled)) return 0;
        // wants CTRL down, wants ALT up.
        if (!(User::GetKeyState(VkControl) & 0x8000)) return 0;
        if (User::GetKeyState(VkMenu) & 0x8000) return 0;
        const bool back = (User::GetKeyState(VkShift) & 0x8000) != 0;
        unsigned command = 0;
        if (message.wparam == VkF6 || message.wparam == VkTab) command = back ? ScPrevWindow : ScNextWindow;
        else if (message.wparam == VkF4) command = ScClose;
        if (!command) return 0;
        // Windows 98 sends, it does not post, and the key travels in lParam.
        Send(child, WmSysCommand, command, message.wparam & 65535);
        return 1;
    }
    default:
        break;
    }
    Fail(name);
    return 0;
}
} // namespace X86
