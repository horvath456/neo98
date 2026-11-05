// SPDX-License-Identifier: GPL-2.0-or-later
// The window arrangement exports of USER32: the icon grid, tiling, cascading,
// the owned popups and the flashing caption. All of them are word thunks of
// USER.EXE segments 13 and 15, and all of them are one enumeration of a
// parent's children plus a rule for where each one goes.
#include "user.h"
#include "x86_app.h"
#include <string.h>
namespace X86 {
namespace {
// Window styles USER and Windows 98 read out of the WND record.
constexpr unsigned StyleDisabled = 0x08000000, StyleVisible = 0x10000000;
constexpr unsigned StyleMinimize = 0x20000000, StyleThickFrame = 0x00040000;
constexpr unsigned StyleCaption = 0x00C00000;
constexpr unsigned ExStyleTopmost = 0x00000008, ExStyleToolWindow = 0x00000080;
// MDITILE_ flags. Windows 98 reads the horizontal bit and the disabled
// one; MDITILE_ZORDER never reaches a branch at all.
constexpr unsigned TileHorizontal = 1, TileSkipDisabled = 2;
// SetWindowPos flags. Windows 98 builds 114h and adds SWP_NOSIZE for a window
// that cannot be resized.
constexpr unsigned PosNoSize = 0x0001, PosNoZOrder = 0x0004;
constexpr unsigned PosNoActivate = 0x0010, PosNoCopyBits = 0x0100;
constexpr unsigned ArrangeFlags = PosNoZOrder | PosNoActivate | PosNoCopyBits;
// System metric indices the arrangement reads.
constexpr unsigned MetricCxSize = 30, MetricCySize = 31;
constexpr unsigned MetricCxFrame = 32, MetricCyFrame = 33;
constexpr unsigned MetricCxMinSpacing = 47, MetricCyMinSpacing = 48;
constexpr unsigned MetricCxMinimized = 57, MetricCyMinimized = 58;
// ShowWindow commands. USER turns the flag into one of these two.
constexpr unsigned ShowParentClosing = 1, ShowParentOpening = 3;
} // namespace

// USER. The minimized children of one parent go onto a grid of
// SM_CXMINSPACING by SM_CYMINSPACING and are resized to SM_CXMINIMIZED by
// SM_CYMINIMIZED. The original starts at the bottom left, the direction that
// SM_ARRANGE reports by default, and lays the icons out row by row.
// The original also skips a window that carries no "SysCP" checkpoint
// property, the record ShowWindow writes when it minimizes a window. NEO98 has
// no such property; being visible and minimized is the whole test here.
unsigned Application::ArrangeIcons(unsigned parent)
{
    if (!User::IsWindow(parent)) { last_error = 1400; return 0; }
    unsigned windows[User::WindowCapacity], count = 0;
    if (!User::WindowSnapshot(parent, windows, User::WindowCapacity, count)) { last_error = 8; return 0; }
    Gdi::RECT area = {};
    User::GetClientRect(parent, &area);
    const int dx = int(SystemMetric(MetricCxMinSpacing)), dy = int(SystemMetric(MetricCyMinSpacing));
    const int cx = int(SystemMetric(MetricCxMinimized)), cy = int(SystemMetric(MetricCyMinimized));
    if (dx <= 0 || dy <= 0) { last_error = 87; return 0; }
    // how many icons fit on one line before the next one starts.
    int per_line = area.bottom / dy;
    if (per_line < 1) per_line = 1;
    int x = area.left, y = area.bottom - dy, left = per_line;
    unsigned arranged = 0;
    for (unsigned i = 0; i < count; ++i)
    {
        if (!User::IsWindow(windows[i])) continue;
        const unsigned style = User::Style(windows[i]);
        if (!(style & StyleVisible) || !(style & StyleMinimize)) continue;
        const unsigned position[] = {windows[i], 0, unsigned(x), unsigned(y),
                                     unsigned(cx), unsigned(cy), ArrangeFlags};
        Api("SetWindowPos", position);
        ++arranged;
        if (--left > 0) x += dx;
        else { left = per_line; x = area.left; y -= dy; }
    }
    // Windows 98 answers the number of icons it arranged.
    return arranged;
}

// USER: which children take part in a tile or a cascade.
bool Application::ArrangeCandidate(unsigned hwnd, unsigned how, unsigned &flags)
{
    if (!User::IsWindow(hwnd)) return false;
    const unsigned style = User::Style(hwnd), extended = User::ExtendedStyle(hwnd);
    if (!(style & StyleVisible)) return false;
    if ((style & StyleCaption) != StyleCaption) return false;   //
    if (style & StyleMinimize) return false;                    //
    if (extended & ExStyleTopmost) return false;                //
    if ((how & TileSkipDisabled) && (style & StyleDisabled)) return false; //
    if (extended & ExStyleToolWindow) return false;             //
    // a window without a sizing border keeps its size.
    flags = ArrangeFlags | ((style & StyleThickFrame) ? 0u : PosNoSize);
    return true;
}

// USER, the kernel both TileWindows and CascadeWindows go through.
unsigned Application::ArrangeWindows(bool cascade, const unsigned *a)
{
    const unsigned parent = a[0] ? a[0] : User::GetDesktopWindow();
    const unsigned how = a[1], rect = a[2], children = a[3], list = a[4];
    if (!User::IsWindow(parent)) { last_error = 1400; return 0; }
    Gdi::RECT area = {};
    if (rect)
    {
        if (!Read(rect, &area, sizeof area)) { last_error = 87; return 0; }
    }
    else
    {
        User::GetClientRect(parent, &area);
        // without a rectangle the icons keep their row.
        if (ArrangeIcons(parent))
        {
            const int dy = int(SystemMetric(MetricCyMinSpacing));
            if (area.bottom - area.top > dy) area.bottom -= dy;
        }
    }
    unsigned windows[User::WindowCapacity], count = 0;
    if (children && list)
    {
        if (children > User::WindowCapacity) { last_error = 87; return 0; }
        if (!Read(list, windows, children * 4)) { last_error = 87; return 0; }
        count = children;
    }
    else if (!User::WindowSnapshot(parent, windows, User::WindowCapacity, count))
    {
        last_error = 8;
        return 0;
    }
    // and Windows 98 walk the list backwards, so the window at the top of
    // the Z-order is placed last and keeps the top.
    unsigned taking[User::WindowCapacity], flags[User::WindowCapacity], n = 0;
    for (unsigned i = count; i--;)
    {
        unsigned position = 0;
        if (!ArrangeCandidate(windows[i], how, position)) continue;
        taking[n] = windows[i];
        flags[n] = position;
        ++n;
    }
    if (!n) return 0;
    const int width = area.right - area.left, height = area.bottom - area.top;
    if (width <= 0 || height <= 0) return 0;
    unsigned placed = 0;
    if (cascade)
    {
        // one step is a caption button plus a frame, the number of
        // steps is a third of the height, and the offset wraps at the end.
        const int step_x = int(SystemMetric(MetricCxSize) + SystemMetric(MetricCxFrame));
        const int step_y = int(SystemMetric(MetricCySize) + SystemMetric(MetricCyFrame));
        const int steps = step_y > 0 ? height / (3 * step_y) : 0;
        const int cx = width - steps * step_x, cy = height - steps * step_y;
        for (unsigned i = 0; i < n; ++i)
        {
            const int j = steps >= 0 ? int(i) % (steps + 1) : 0;
            const unsigned position[] = {taking[i], 0, unsigned(area.left + j * step_x),
                                         unsigned(area.top + j * step_y),
                                         unsigned(cx), unsigned(cy), flags[i]};
            Api("SetWindowPos", position);
            ++placed;
        }
        return placed;
    }
    // the grid is as square as the count allows, and the remainder
    // gives the last columns one row more.
    unsigned d = 2;
    while (d * d <= n) ++d;
    --d;
    const unsigned columns = (how & TileHorizontal) ? d : n / d;
    const unsigned rows = (how & TileHorizontal) ? n / d : d;
    unsigned extra = n % d, taken = 0;
    for (unsigned column = 0; column < columns; ++column)
    {
        const unsigned lines = rows + ((extra >= columns - column) ? 1u : 0u);
        if (!lines) continue;
        const int cell_w = width / int(columns), cell_h = height / int(lines);
        for (unsigned row = 0; row < lines && taken < n; ++row, ++taken)
        {
            const unsigned position[] = {taking[taken], 0,
                                         unsigned(area.left + int(column) * cell_w),
                                         unsigned(area.top + int(row) * cell_h),
                                         unsigned(cell_w), unsigned(cell_h), flags[taken]};
            Api("SetWindowPos", position);
            ++placed;
        }
        if (extra >= columns - column) --extra;
    }
    return placed;
}

unsigned Application::ArrangeApi(ApiName name, const unsigned *a)
{
    switch (name.id)
    {
    // USER over the worker above.
    API_CASE("ArrangeIconicWindows") return ArrangeIcons(a[0]);
    API_CASE("TileWindows") return ArrangeWindows(false, a);
    API_CASE("CascadeWindows") return ArrangeWindows(true, a);
    // USER over. The snapshot of the top-level windows is
    // taken before anything moves, and every window whose owner chain ends at
    // the given one gets WM_SHOWWINDOW with SW_PARENTOPENING or
    // SW_PARENTCLOSING. In Win98 the show and the hide are the work of
    // DefWindowProc's WM_SHOWWINDOW handler, which also keeps the marker that
    // says "this popup was hidden by ShowOwnedPopups". NEO98 keeps that marker
    // in its own set and does the show and the hide here; the message still
    // goes out first, so a window that watches for it sees the original order.
    API_CASE("ShowOwnedPopups")
    {
        if (!User::IsWindow(a[0])) { last_error = 1400; return 0; }
        const bool show = a[1] != 0;
        unsigned windows[User::WindowCapacity], count = 0;
        if (!User::WindowSnapshot(0, windows, User::WindowCapacity, count)) { last_error = 8; return 0; }
        for (unsigned i = 0; i < count; ++i)
        {
            if (!User::IsWindow(windows[i]) || windows[i] == a[0]) continue;
            unsigned owner = User::Owner(windows[i]), guard = 0;
            while (owner && owner != a[0] && ++guard < User::WindowCapacity)
                owner = User::Owner(owner);
            if (owner != a[0]) continue;
            const bool visible = (User::Style(windows[i]) & StyleVisible) != 0;
            // showing only touches what this call hid, hiding only
            // what is visible.
            bool hidden = false;
            for (unsigned slot = 0; slot < HiddenPopupCapacity; ++slot)
                if (hidden_popups[slot] == windows[i]) { hidden = true; break; }
            if (show ? !hidden : !visible) continue;
            Send(windows[i], 0x18, a[1] ? 1u : 0u, show ? ShowParentOpening : ShowParentClosing);
            if (!User::IsWindow(windows[i])) continue;
            User::ShowWindow(windows[i], show ? 4 : 0); // SW_SHOWNOACTIVATE / SW_HIDE
            for (unsigned slot = 0; slot < HiddenPopupCapacity; ++slot)
            {
                if (show && hidden_popups[slot] == windows[i]) { hidden_popups[slot] = 0; break; }
                if (!show && !hidden_popups[slot]) { hidden_popups[slot] = windows[i]; break; }
            }
        }
        // always answers one.
        return 1;
    }
    // USER builds a fourteen byte FLASHWINFO with FLASHW_ALL and a
    // count of zero and hands it to the machine; bInvert zero means
    // FLASHW_STOP. The machine toggles the caption with WM_NCACTIVATE and
    // answers the state the caption had on entry.
    API_CASE("FlashWindow")
    {
        if (!User::IsWindow(a[0])) { last_error = 1400; return 0; }
        const bool was_active = FlashWindowState(a[0], false, false);
        if (!a[1])
        {
            // restores the caption of the window that really is active.
            const unsigned active = Api("GetActiveWindow", nullptr);
            FlashWindowState(a[0], true, active == a[0]);
            Send(a[0], 0x86, active == a[0] ? 1u : 0u, 0);
            return was_active ? 1u : 0u;
        }
        // while flashing, the caption takes the other state.
        FlashWindowState(a[0], true, !was_active);
        Send(a[0], 0x86, was_active ? 0u : 1u, 0);
        return was_active ? 1u : 0u;
    }
    // USER over the third thunk table. The record check is
    // there, the machine is the worker above.
    API_CASE("FlashWindowEx") return FlashWindowEx(a[0]);
    // USER32 ShowWindowAsync posts the show to the owning thread instead of
    // performing it. NEO98 runs every GUI thread on one native queue, so there
    // is no other thread to hand it to and the show happens at once.
    API_CASE("ShowWindowAsync")
    {
        if (!User::IsWindow(a[0])) { last_error = 1400; return 0; }
        const unsigned arguments[] = {a[0], a[1]};
        return Api("ShowWindow", arguments);
    }
    default:
        break;
    }
    Fail(name);
    return 0;
}

// The state Windows 98 keeps in the window property "SysFWStat". The original
// packs it into one DWORD: bit 8000h says the caption is drawn active, bit
// 1000h remembers the caption state at the moment the flashing started, bit
// 4000h says a timer runs, the low bits 000Bh hold the stored FLASHW_ bits and
// the high word holds the remaining count. NEO98 keeps the same fields in a
// small table beside the hidden popups.
Application::FlashState *Application::FlashRecord(unsigned hwnd, bool create)
{
    for (auto &record : flash_states)
        if (record.hwnd == hwnd) return &record;
    if (!create) return nullptr;
    for (auto &record : flash_states)
        if (!record.hwnd)
        {
            record = FlashState{};
            record.hwnd = hwnd;
            return &record;
        }
    return nullptr;
}

bool Application::FlashWindowState(unsigned hwnd, bool write, bool value)
{
    FlashState *record = FlashRecord(hwnd, write);
    if (!record) return false;
    if (!write) return record->active;
    record->active = value;
    return value;
}

// stopping kills the timer USER started and drops the
// property. The timer id FFF5h and the property both belong to USER, so no
// guest ever sees them.
void Application::FlashStop(FlashState &record)
{
    if (record.running)
    {
        const unsigned kill[] = {record.hwnd, FlashTimerId};
        Api("KillTimer", kill);
    }
    record = FlashState{};
}

// USER checks the record and hands the whole machine the
// window. The 16-bit record is fourteen bytes; the one a 32-bit caller fills
// is twenty, and the flat thunk carries the size field through unchanged.
unsigned Application::FlashWindowEx(unsigned info)
{
    unsigned record[5] = {};
    if (!Read(info, record, sizeof record)) { last_error = 87; return 0; }
    // A wrong size and any bit outside the four public
    // FLASHW_ bits and the six USER keeps for itself are parameter errors.
    if (record[0] != sizeof record) { last_error = 87; return 0; }
    if (record[2] & 0xffff03f0u) { last_error = 87; return 0; }
    const unsigned hwnd = record[1];
    if (!User::IsWindow(hwnd)) { last_error = 1400; return 0; }
    FlashState *state = FlashRecord(hwnd, false);
    // Windows 98 answers bit 8000h of the stored state, so a window that never
    // flashed answers zero.
    const unsigned entered = state && state->active ? 1u : 0u;
    // Windows 98 leaves at once for the window that already owns the foreground.
    if (hwnd == User::GetForegroundWindow()) return entered;
    unsigned flags = record[2] & FlashPublicBits;
    unsigned count = record[3];
    // a window that is already flashing keeps the flags and the count
    // it was started with. Only a stop can change them.
    if (flags && state) { flags = state->flags; count = state->count; }
    const bool foreground = User::WindowProcess(hwnd) &&
                            User::WindowProcess(User::GetForegroundWindow()) == User::WindowProcess(hwnd);
    // FLASHW_TIMERNOFG and a stop that is already pending both end
    // the flashing as soon as the window's own task owns the foreground.
    if (state && (state->flags & FlashTimerNoForeground) && foreground) flags = 0;
    // While flashing the caption takes the other state,
    // and a stop puts back the state the window really has.
    const bool desired = flags ? !entered : Api("GetActiveWindow", nullptr) == hwnd;
    // the caption moves for a stop and for FLASHW_CAPTION.
    if (!flags || (flags & FlashCaption)) Send(hwnd, 0x86, desired ? 1u : 0u, 0);
    if (!User::IsWindow(hwnd)) return entered;
    if (!flags)
    {
        if (state) FlashStop(*state);
        return entered;
    }
    state = FlashRecord(hwnd, true);
    if (!state) { last_error = 8; return entered; }
    if (!state->flags)
    {
        // the first call writes 9000h, so bit 1000h freezes the
        // caption state the window had when the flashing began.
        state->origin = entered != 0;
        state->active = entered != 0;
    }
    // the count falls by one whenever the caption returns to the
    // state it had at the start, so it counts whole flashes, not half ones.
    bool exhausted = false;
    if (count)
    {
        if (desired == state->origin && --count == 0) exhausted = true;
    }
    // arms the timer; Windows 98 uses the caret blink rate of the system,
    // not the dwTimeout field, which never reads.
    if ((flags & FlashTimer) && !state->running && !exhausted)
    {
        const unsigned set[] = {hwnd, FlashTimerId, caret_blink, FlashTimerProcedure};
        if (Api("SetTimer", set)) state->running = true;
    }
    state->flags = flags & FlashStoredBits;
    state->count = count;
    state->active = desired;
    // the last counted flash ends the run. FLASHW_TRAY without the
    // caption bit waits for the foreground instead.
    if (exhausted)
    {
        if ((flags & FlashTray) && !foreground) state->flags |= FlashTimerNoForeground;
        else FlashStop(*state);
    }
    return entered;
}

// The tick of the timer Windows 98 set. In Win98 the timer procedure lives in
// USER, so DispatchMessage calls it and the program never sees WM_TIMER.
void Application::FlashTick(unsigned hwnd)
{
    FlashState *state = FlashRecord(hwnd, false);
    if (!state) return;
    if (!User::IsWindow(hwnd)) { *state = FlashState{}; return; }
    unsigned record[5] = {sizeof record, hwnd, state->flags | FlashTimer, state->count, 0};
    const unsigned block = Allocate(sizeof record);
    if (!block) return;
    if (Store(block, record, sizeof record)) FlashWindowEx(block);
    Free(block);
}
} // namespace X86
