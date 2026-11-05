// SPDX-License-Identifier: GPL-2.0-or-later
// The nine USER32 information queries of USER.EXE segment 28: GetWindowInfo,
// GetTitleBarInfo, GetScrollBarInfo, GetComboBoxInfo, GetListBoxInfo,
// GetCursorInfo, GetAltTabInfo, GetGUIThreadInfo and RealGetWindowClass. Only
// games/KIDS200/INST/G016/main.exe binds them, but they read nothing the port
// does not already keep, so they answer out of its own state instead of
// rebuilding the 16-bit routines. What each one narrows stands in
//
#include "user.h"
#include "x86_app.h"
#include <string.h>
namespace X86 {
namespace {
// The state bits of the accessibility structures, USER onwards.
constexpr unsigned StateUnavailable = 0x00000001, StatePressed = 0x00000008;
constexpr unsigned StateInvisible = 0x00008000, StateOffscreen = 0x00010000;
constexpr unsigned StateFocusable = 0x00100000;
// Window styles the three frame queries read.
constexpr unsigned StyleMinimize = 0x20000000, StyleCaption = 0x00C00000;
constexpr unsigned StyleSysMenu = 0x00080000, StyleBorder = 0x00800000;
constexpr unsigned StyleDlgFrame = 0x00400000, StyleThickFrame = 0x00040000;
constexpr unsigned StyleMinimizeBox = 0x00020000, StyleMaximizeBox = 0x00010000;
constexpr unsigned ExStyleDlgModalFrame = 0x00000001, ExStyleStaticEdge = 0x00020000;
constexpr unsigned ExStyleToolWindow = 0x00000080, ExStyleWindowEdge = 0x00000100;
constexpr unsigned ExStyleContextHelp = 0x00000400;
// System metric indices.
constexpr unsigned MetricCxBorder = 5, MetricCyBorder = 6;
constexpr unsigned MetricCyCaption = 4, MetricCySmallCaption = 51;
// Nonclient hit codes the caption buttons carry.
constexpr unsigned HitMinButton = 8, HitMaxButton = 9, HitClose = 20, HitHelp = 21;
// GUITHREADINFO flags, onwards.
constexpr unsigned GuiCaretBlinking = 1, GuiInMoveSize = 2, GuiInMenuMode = 4;
constexpr unsigned GuiSystemMenuMode = 8, GuiPopupMenuMode = 0x10;
} // namespace

// USER with fClientEdge zero and fFrame one: how many border units the
// frame of this window is wide. GetWindowInfo scales the count with
// SM_CXBORDER and SM_CYBORDER, and GetTitleBarInfo shrinks the window
// rectangle by the same amount. WS_EX_CLIENTEDGE deliberately does not count.
unsigned Application::FrameBorderUnits(unsigned style, unsigned extended)
{
    unsigned units = 0;
    if (extended & ExStyleWindowEdge) units = 2;
    else if (extended & ExStyleStaticEdge) units = 1;
    if ((style & (StyleBorder | StyleDlgFrame)) || (extended & ExStyleDlgModalFrame)) units += 1;
    // Windows 98 adds the configurable sizing border, one unit by default.
    if (style & StyleThickFrame) units += 1;
    return units;
}

unsigned Application::WindowInfoApi(ApiName name, const unsigned *a)
{
    switch (name.id)
    {
    // USER. Window and client rectangle in screen coordinates, both
    // style words, the active-caption bit, the frame width in pixels, the
    // class atom and the Windows version of the creating module.
    API_CASE("GetWindowInfo")
    {
        if (!User::IsWindow(a[0])) return 0;
        unsigned size = 0;
        if (!a[1] || !Read(a[1], &size, sizeof size) || size != 0x3c) return 0;
        Gdi::RECT window{}, client{};
        if (!User::GetWindowRect(a[0], &window) || !User::ClientScreenRect(a[0], &client)) return 0;
        const unsigned style = User::Style(a[0]), extended = User::ExtendedStyle(a[0]);
        const unsigned units = FrameBorderUnits(style, extended);
        unsigned record[15] = {size,
            unsigned(window.left), unsigned(window.top), unsigned(window.right), unsigned(window.bottom),
            unsigned(client.left), unsigned(client.top), unsigned(client.right), unsigned(client.bottom),
            style, extended,
            // WS_ACTIVECAPTION is the only status bit the original sets.
            User::GetForegroundWindow() == a[0] ? 1u : 0u,
            units * SystemMetric(MetricCxBorder), units * SystemMetric(MetricCyBorder),
            // packs the class atom and the creator version into one
            // dword. A 16-bit task is Windows 3.1, everything else 4.0.
            (User::ClassAccess(a[0], -32, 0, 0) & 0xffffu) |
                ((Win16Process() ? 0x030au : 0x0400u) << 16)};
        return Store(a[1], record, sizeof record) ? 1 : 0;
    }
    // USER. The caption band and the six accessibility states of the
    // bar and its buttons.
    API_CASE("GetTitleBarInfo")
    {
        if (!User::IsWindow(a[0])) return 0;
        unsigned size = 0;
        if (!a[1] || !Read(a[1], &size, sizeof size) || size != 0x2c) return 0;
        const unsigned style = User::Style(a[0]), extended = User::ExtendedStyle(a[0]);
        unsigned record[11] = {size, 0, 0, 0, 0, StateFocusable, 0, 0, 0, 0, 0};
        Gdi::RECT bar{};
        if (!Read(a[1] + 4, record + 1, 16)) return 0;
        // without a full caption style the bar is invisible and the
        // rectangle stays untouched.
        if ((style & StyleCaption) != StyleCaption)
        {
            record[5] |= StateInvisible;
            return Store(a[1], record, sizeof record) ? 1 : 0;
        }
        // a caption the frame does not carry right now is offscreen,
        // and the rectangle is emptied.
        if (!(style & StyleMinimize) && !User::CaptionBar(a[0], &bar))
        {
            record[5] |= StateOffscreen;
            record[1] = record[2] = record[3] = record[4] = 0;
            return Store(a[1], record, sizeof record) ? 1 : 0;
        }
        if (!User::CaptionBar(a[0], &bar))
        {
            // A minimized window has no band of its own; Windows 98 builds one
            // out of the window rectangle and the caption height.
            if (!User::GetWindowRect(a[0], &bar)) return 0;
            const unsigned units = FrameBorderUnits(style, extended);
            bar.left += int(units * SystemMetric(MetricCxBorder));
            bar.right -= int(units * SystemMetric(MetricCxBorder));
            bar.top += int(units * SystemMetric(MetricCyBorder));
            bar.bottom = bar.top + int(SystemMetric((extended & ExStyleToolWindow)
                                                    ? MetricCySmallCaption : MetricCyCaption));
            // indents past the system-menu icon.
            if (style & StyleSysMenu) bar.left += (bar.bottom - bar.top) - int(SystemMetric(MetricCyBorder));
        }
        record[1] = unsigned(bar.left); record[2] = unsigned(bar.top);
        record[3] = unsigned(bar.right); record[4] = unsigned(bar.bottom);
        const unsigned pressed = User::CaptionPressed(a[0]);
        // The two size buttons grey each other out, and
        // a Windows 4.0 window without WS_SYSMENU shows none of the three.
        auto box = [&](unsigned present, unsigned other, unsigned hit) {
            unsigned state = 0;
            if (!(style & StyleSysMenu)) state = StateInvisible;
            else if (!(style & present)) state = (style & other) ? StateUnavailable : StateInvisible;
            if (pressed == hit) state |= StatePressed;
            return state;
        };
        record[6] = StateInvisible;   // the reserved element
        record[7] = box(StyleMinimizeBox, StyleMaximizeBox, HitMinButton);
        record[8] = box(StyleMaximizeBox, StyleMinimizeBox, HitMaxButton);
        // the help button shows only without the two size buttons.
        record[9] = (extended & ExStyleContextHelp) &&
                    !(style & (StyleMinimizeBox | StyleMaximizeBox))
                        ? (pressed == HitHelp ? StatePressed : 0u) : StateInvisible;
        // over : the close button greys out without an enabled
        // SC_CLOSE in the system menu.
        record[10] = (style & StyleSysMenu) ? 0u : StateInvisible;
        if (record[10] == 0)
        {
            const unsigned menu = User::SystemMenuRoot(a[0]);
            const unsigned state = menu ? User::MenuState(menu, 0xf060, 0) : ~0u;
            if (!menu || state == ~0u || (state & 3)) record[10] = StateUnavailable;
        }
        if (pressed == HitClose) record[10] |= StatePressed;
        return Store(a[1], record, sizeof record) ? 1 : 0;
    }
    // USER. One bar of one window, or the single bar of a SCROLLBAR
    // control when the caller names OBJID_CLIENT.
    API_CASE("GetScrollBarInfo")
    {
        if (!User::IsWindow(a[0])) return 0;
        unsigned size = 0;
        if (!a[2] || !Read(a[2], &size, sizeof size) || size != 0x3c) return 0;
        const int object = int(a[1]);
        unsigned record[15] = {size, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        if (!Read(a[2] + 4, record + 1, 32)) return 0;
        for (unsigned i = 9; i < 15; ++i) record[i] = 0;
        User::ScrollBarLayoutView view;
        if (!User::ScrollBarLayout(a[0], object, view)) return 0;
        // no style at all means invisible and nothing else is filled.
        if (object != -4 && !view.styled)
        {
            record[9] = StateInvisible;
            return Store(a[2], record, sizeof record) ? 1 : 0;
        }
        if (!view.present) record[9] = StateOffscreen;
        // the disabled ends grey out their arrow and their page.
        if (view.disabled & 1) { record[10] |= StateUnavailable; record[11] |= StateUnavailable; record[9] |= StateUnavailable; }
        if (view.disabled & 2) { record[13] |= StateUnavailable; record[14] |= StateUnavailable; }
        if (view.pressed & 1) record[10] |= StatePressed;
        if (view.pressed & 2) record[11] |= StatePressed;
        if (view.pressed & 4) record[13] |= StatePressed;
        if (view.pressed & 8) record[14] |= StatePressed;
        if (!view.present) return Store(a[2], record, sizeof record) ? 1 : 0;
        record[1] = unsigned(view.bar.left); record[2] = unsigned(view.bar.top);
        record[3] = unsigned(view.bar.right); record[4] = unsigned(view.bar.bottom);
        record[5] = unsigned(view.line);
        record[6] = unsigned(view.thumb_start);
        record[7] = unsigned(view.thumb_end);
        // an empty page region is reported as invisible.
        if (view.empty_first) record[11] |= StateInvisible;
        if (view.empty_last) record[13] |= StateInvisible;
        return Store(a[2], record, sizeof record) ? 1 : 0;
    }
    // USER. The two rectangles of a combo box, the state of its drop
    // down button and its three windows. Windows 98 accepts the handle of the
    // combo's own list box as well and answers for the combo behind it.
    API_CASE("GetComboBoxInfo")
    {
        if (!User::IsWindow(a[0])) return 0;
        unsigned size = 0;
        if (!a[1] || !Read(a[1], &size, sizeof size) || size != 0x34) return 0;
        unsigned view[12] = {};
        if (!ComboBoxView(a[0], view)) return 0;
        unsigned record[13] = {size, view[0], view[1], view[2], view[3],
                               view[4], view[5], view[6], view[7],
                               view[8], view[9], view[10], view[11]};
        return Store(a[1], record, sizeof record) ? 1 : 0;
    }
    // USER. The items per column of a multi-column list and the whole
    // count of every other one.
    API_CASE("GetListBoxInfo") return ListBoxItemsPerColumn(a[0]);
    // USER. The cursor the display really shows, its visibility and
    // its position.
    API_CASE("GetCursorInfo")
    {
        unsigned size = 0;
        if (!a[0] || !Read(a[0], &size, sizeof size) || size != 0x14) return 0;
        Gdi::POINT p{};
        User::CursorPosition(&p);
        unsigned record[5] = {size, User::PointerVisible() ? 1u : 0u,
                              User::PointerShape(), unsigned(p.x), unsigned(p.y)};
        return Store(a[0], record, sizeof record) ? 1 : 0;
    }
    // USER answers only while USER's own Alt+Tab window is up, and
    // only for that window. NEO98 has no Alt+Tab switcher, so the state flag
    // at USER's data 248h is always zero and the query always fails, which is
    // the answer Win98 gives whenever the switcher is not on the screen.
    API_CASE("GetAltTabInfo") return 0;
    // USER. The windows of one thread's input state.
    API_CASE("GetGUIThreadInfo")
    {
        unsigned size = 0;
        if (!a[1] || !Read(a[1], &size, sizeof size) || size != 0x30) return 0;
        // without a thread the caller means its own, and a thread
        // this process does not run fails, the way an unknown queue does.
        const unsigned thread = a[0] ? a[0] : ThreadId();
        bool known = false;
        for (const auto &t : threads)
            if (t.id == thread && t.fiber && !t.done) { known = true; break; }
        if (!known) return 0;
        unsigned flags = 0, menu_owner = 0;
        // a running menu sets GUI_INMENUMODE, and a popup adds
        // GUI_POPUPMENUMODE. NEO98 tracks one popup session at a time.
        if (User::PopupActive())
        {
            flags |= GuiInMenuMode | GuiPopupMenuMode;
            menu_owner = User::PopupWindowForMenu(0);
        }
        const unsigned move_size = User::SizeMoveWindow();
        if (move_size) flags |= GuiInMoveSize;
        Gdi::RECT caret_rect{};
        unsigned caret_window = 0;
        if (caret.owner && User::IsWindow(caret.owner))
        {
            caret_window = caret.owner;
            caret_rect = {int(caret.x), int(caret.y),
                          int(caret.x + caret.width), int(caret.y + caret.height)};
            // the bit says the caret is not hidden, not that it is
            // drawn at this instant.
            if (!caret.hides) flags |= GuiCaretBlinking;
        }
        const unsigned none[1] = {0};
        unsigned record[12] = {size, flags, Api("GetActiveWindow", none), User::GetFocus(),
                               User::CaptureOperation(0), menu_owner, move_size, caret_window,
                               unsigned(caret_rect.left), unsigned(caret_rect.top),
                               unsigned(caret_rect.right), unsigned(caret_rect.bottom)};
        return Store(a[1], record, sizeof record) ? 1 : 0;
    }
    // USER. The atom the window itself carries, which is the class
    // atom for everything NEO98 registers: the port has no separate slot that
    // a superclassed built-in control could overwrite, so the answer is the
    // one GetClassName gives.
    API_CASE("RealGetWindowClass")
    {
        if (!User::IsWindow(a[0]) || !a[1] || !a[2]) return 0;
        char name[256] = {};
        const unsigned length = User::GetClassName(a[0], name, sizeof name);
        if (!length) return 0;
        const unsigned copied = length < a[2] ? length : a[2] - 1;
        name[copied] = 0;
        return Store(a[1], name, copied + 1) ? copied : 0;
    }
    default:
        break;
    }
    Fail(name);
    return 0;
}
} // namespace X86
