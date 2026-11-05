// SPDX-License-Identifier: GPL-2.0-or-later
// The USER32 exports that are 32-to-16 gates into USER.EXE and were still
// missing from the export contract on 23 September 2026 (branch dlls, phase
// 2). Each entry names the USER32 stub and the USER.EXE routine it follows.
// The three DDEML gates (DdeAddData, DdeEnableCallback, DdeReconnect) live
// with the rest of DDE in src/x86_dde.cpp.
#include "x86_app.h"
#include "win98_cursor.h"
#include "sound.h"
#include <string.h>
namespace X86
{
namespace
{
// USER's power words of segment 15 and 16: [E6h] sleep started, [ECh] resume
// automatic, [E8h] and [EAh] the system and display holds. No power manager
// drives them in this port, but the entries that write them do.
struct PowerState
{
    unsigned sleep_started = 0, resume_automatic = 0, system_hold = 0, display_hold = 0;
    unsigned long long system_idle = 0, display_idle = 0;
};
PowerState power;
// USER: the string of each sound event id, LoadString(USER, id).
const char *SoundEventName(unsigned id)
{
    static const char *const names[] = {"SystemStart", "SystemExit", ".Default", "SystemHand",
                                        "SystemQuestion", "SystemExclamation", "SystemAsterisk",
                                        "Maximize", "Minimize", "RestoreUp", "RestoreDown", "Open",
                                        "AppGPFault", "Close", "MenuCommand", "MenuPopup"};
    return id >= 0x8f && id <= 0x9e ? names[id - 0x8f] : nullptr;
}
} // namespace

unsigned Application::User32GateApi(ApiName name, const unsigned *a)
{
    switch (name.id)
    {
    // CL A6h, USER: the first top-level window with an
    // owner that is visible.
    API_CASE("AnyPopup")
    {
        unsigned windows[256] = {}, count = 0;
        if (!User::WindowSnapshot(User::GetDesktopWindow(), windows, 256, count))
            return 0;
        for (unsigned i = 0; i < count; ++i)
            if (User::Owner(windows[i]) && User::IsVisible(windows[i]))
                return 1;
        return 0;
    }
    // The cascade and tile workers with no rectangle and no window list.
    API_CASE("CascadeChildWindows") API_CASE("TileChildWindows")
    {
        const unsigned arguments[5] = {a[0] & 0xffff ? a[0] : 0, a[1] & 0xffff, 0, 0, 0};
        return unsigned(int(short(ArrangeWindows(API_IS(name, "CascadeChildWindows"), arguments))));
    }
    // KEYBOARD.132 : the OEM code page of the keyboard layout, 437 in
    // the file and overwritten by the loaded layout table.
    API_CASE("GetKBCodePage")
        return Api("GetOEMCP", a) & 0xffff;
        // CL 41h, USER over : the Alt+Esc walk from the
    // top-level window of `hwnd`, forwards or backwards, skipping invisible
    // windows and those whose last active popup is disabled, and taking the
    // window that is its root owner's last active popup.
    API_CASE("GetNextQueueWindow")
    {
        unsigned hwnd = a[0];
        if (!User::IsWindow(hwnd))
            return 0;
        while (User::Parent(hwnd) && User::Parent(hwnd) != User::GetDesktopWindow())
            hwnd = User::Parent(hwnd);
        const unsigned desktop = User::GetDesktopWindow(), start = hwnd;
        unsigned current = hwnd;
        for (unsigned guard = 0; guard < 4096; ++guard)
        {
            unsigned next = User::GetWindow(current, (a[1] & 0xffff) ? 3 : 2);
            if (!next)
                next = (a[1] & 0xffff) ? User::GetWindow(User::GetWindow(desktop, 5), 1)
                                        : User::GetWindow(desktop, 5);
            current = next;
            if (!current || current == start)
                return 0;
            if (!User::IsVisible(current))
                continue;
            const unsigned last[1] = {current};
            const unsigned popup = Api("GetLastActivePopup", last);
            if (popup && !User::IsEnabled(popup))
                continue;
            unsigned root = current;
            while (User::Owner(root))
                root = User::Owner(root);
            const unsigned root_last[1] = {root};
            if (Api("GetLastActivePopup", root_last) == current)
                return current;
        }
        return 0;
    }
    // CL 0Bh, USER: one thread may block input; only that
    // thread lifts the block. The block keeps the host's hardware input away;
    // input a program injects passes (src/x86_app.cpp, RawKey and
    // MouseButtons).
    API_CASE("BlockInput")
    {
        const unsigned owner = User::InputBlocked();
        if (a[0] & 0xffff)
        {
            if (owner)
                return 0;
            User::SetInputBlocked(ThreadId());
            return 1;
        }
        if (!owner || owner != ThreadId())
            return 0;
        User::SetInputBlocked(0);
        return 1;
    }
    // CL 0Ah, USER: INPUT records of 1Ch bytes go to the
    // driver entries of mouse_event and keybd_event under a
    // block of input; the count of records injected comes back.
    API_CASE("SendInput")
    {
        const unsigned count = a[0] & 0xffff, records = a[1], size = a[2] & 0xffff;
        if (size != 0x1c || !count || !virtual_memory.Access(records, (count * 0x1c) & 0xffff, false))
            return 0;
        const bool owned = !User::InputBlocked();
        if (owned)
            User::SetInputBlocked(ThreadId());
        else if (User::InputBlocked() != ThreadId())
            return 0;
        unsigned done = 0;
        for (; done < count; ++done)
        {
            unsigned record[7] = {};
            if (!User::InputBlocked() || !Read(records + done * 0x1c, record, sizeof record))
                break;
            if (record[0] == 0)
            {
                const unsigned mouse[5] = {record[4], record[1], record[2], record[3], record[6]};
                User32ImportApi("mouse_event", mouse);
            }
            else if (record[0] == 1)
            {
                const unsigned key[4] = {record[1] & 0xffff, (record[1] >> 16) & 0xff, record[2], record[4]};
                User32ImportApi("keybd_event", key);
            }
            else if (record[0] == 2)
            {
                Fail("SendInput with INPUT_HARDWARE: USER is not ported");
                break;
            }
            else
                break;
        }
        if (owned)
            User::SetInputBlocked(0);
        return done;
    }
    // CL 1Dh, USER over : the cursor is copied into
    // the slot of the OEM cursor `id`; the handle passed in stays the
    // caller's. The pointer shows the copy at once if the old one was up.
    API_CASE("SetSystemCursor")
    {
        if (!FindIcon(a[0]))
            return 0;
        const unsigned index = Win98Cursor::Index(a[1]);
        if (index >= 16)
            return 0;
        const unsigned copy_args[5] = {a[0], 2, 0, 0, 0};
        const unsigned copy = CopyImageApi(copy_args);
        if (!copy)
            return 0;
        const unsigned old = system_cursors[index];
        system_cursors[index] = copy;
        if (User::PointerShape() == old)
            User::SetPointerShape(copy);
        return 1;
    }
    // CL C5h, USER: the swap word of the input side and
    // SM_SWAPBUTTON ([30Ch], metric 23) take the new value; the previous raw
    // word is the answer.
    API_CASE("SwapMouseButton")
        return User::SwapMouseButtons(a[0] & 0xffff);
        // CL 26h, USER over : a thread with a top-level
    // window whose queue has not been read for 20 000 ms is hung.
    API_CASE("IsHungThread")
        return ThreadHung(a[0], 20000) ? 1 : 0;
        // CL 0Ah, USER: the event id is the string id of its
    // alias in USER; the sound goes through PlaySound with the flags of the
    // table there. FFFFh without PlaySound is the speaker.
    API_CASE("PlaySoundEvent")
        return PlaySoundEvent(a[0] & 0xffff);
    API_CASE("HasSystemSleepStarted")
        return power.sleep_started;
    API_CASE("UserIsSystemResumeAutomatic")
        return power.resume_automatic;
        // CL 2, USER: bit 1 resets the system idle time, bit 2
    // the display idle time. The stub leaves EAX as the 16-bit side left it.
    API_CASE("UserTickleTimer")
    {
        const unsigned long long now = Now();
        if (a[0] & 1) power.system_idle = now;
        if (a[0] & 2) power.display_idle = now;
        return 0;
    }
    // CL 1, USER: bit 1 is the system hold, bit 2 the
    // display hold.
    API_CASE("UserSetDeviceHoldState")
    {
        if (a[0] & 1) power.system_hold = a[1] & 0xffff;
        if (a[0] & 2) power.display_hold = a[1] & 0xffff;
        return 0;
    }
    // CL 7, USER: nine cases of the old-application
    // support. 4 and 5 are the switch into and out of a full-screen DOS box,
    // which this port does not have; 7, 8 and 9 answer from the holds.
    API_CASE("WinOldAppHackoMatic")
    {
        const unsigned n = a[0];
        switch (n)
        {
        case 1: case 2: case 3: case 6:
            return 0;
        case 4: case 5:
            Fail("WinOldAppHackoMatic 4 and 5 switch a full-screen MS-DOS box; this port runs DOS in a window");
            return 0;
        case 7: case 9:
            return power.display_hold ? 1 : 0;
        case 8:
            if (power.system_hold || power.display_hold)
                return 1;
            power.sleep_started = 1;
            return 0;
        default:
            return 0;
        }
    }
    // CL 0Ch, USER: the per-process and per-thread
    // notifications of KERNEL32. Process end plays "Close" and a loaded process
    // "Open"; the rest ends in GDI.610, which answers one.
    API_CASE("UserSignalProc")
    {
        if (a[0] == 0x300 && !(a[2] & 8))
            PlaySoundEvent(0x9c);
        else if (a[0] == 0x600)
            PlaySoundEvent(0x9a);
        return 1;
    }
    // CL 43h, USER over.
    API_CASE("DragDetect")
        return DragDetect(a[0], int(a[1]), int(a[2]));
        // USER over.
    API_CASE("DrawMenuBarTemp")
    {
        if (!User::IsWindow(a[0]) || !Gdi::ValidDC(a[1]))
            return 0;
        Gdi::RECT rect{};
        if (a[2])
        {
            int wide[4];
            if (!Read(a[2], wide, sizeof wide))
                return 0;
            rect = {short(wide[0]), short(wide[1]), short(wide[2]), short(wide[3])};
        }
        return unsigned(int(short(User::DrawMenuBarTemp(a[0], a[1], rect, a[3], a[4]))));
    }
    // CL 8, USER: a box USER draws itself onto the desktop,
    // with its own loop over raw input. Not built yet.
    API_CASE("SysErrorBox")
        Fail("SysErrorBox is not built yet: the self-drawn system-modal box of USER");
        return 0;
        // CL 42h, USER over.
    API_CASE("DragObject")
        return DragObject(a[0], a[1], a[2], a[3], a[4]);
    default:
        break;
    }
    Fail(name);
    return 0;
}

// USER. `id` is the string id of the event's alias in USER.
unsigned Application::PlaySoundEvent(unsigned id)
{
    const char *alias = SoundEventName(id);
    if (id == 0xffff)
    {
        Sound::Speaker::Tone(748, 40);
        return 1;
    }
    if (!alias)
        return 0;
    unsigned flags = (id >= 0x91 && id <= 0x95) ? 0x10010003u : id == 0x90 ? 0x20010012u : 0x10010013u;
    if (id == 0x9a || id == 0x9b || id == 0x9c)
        flags |= 0x20000000u;
    if (id != 0x9b)
        flags |= 0x80u;
    const unsigned text = Allocate(0x4c);
    if (!text)
        return 0;
    Store(text, alias, (unsigned)strlen(alias) + 1);
    const unsigned play[3] = {text, 0, flags};
    const unsigned result = Api("PlaySoundA", play);
    Free(text);
    return result;
}

// USER: the queue of the thread, a top-level window on it, and the
// time since the thread last read its queue.
bool Application::ThreadHung(unsigned thread_id, unsigned milliseconds)
{
    unsigned windows[256] = {}, count = 0;
    bool owns = false;
    if (User::WindowSnapshot(User::GetDesktopWindow(), windows, 256, count))
        for (unsigned i = 0; i < count && !owns; ++i)
            owns = User::WindowThread(windows[i]) == thread_id;
    if (!owns)
        return false;
    const unsigned long long last = User::QueueReadTime(thread_id);
    return last && Now() - last > (unsigned long long)milliseconds * 1000;
}

// USER with WM_LBUTTONUP as the release: capture, then the mouse and
// key messages until the pointer leaves the drag rectangle (TRUE), the
// button comes up, Esc is pressed or the capture is lost (FALSE).
unsigned Application::DragDetect(unsigned hwnd, int x, int y)
{
    if (!User::IsWindow(hwnd))
        return 0;
    const unsigned key[1] = {1};
    if (!(Api("GetKeyState", key) & 0x8000))
        return 0;
    const unsigned capture[1] = {hwnd};
    Api("SetCapture", capture);
    const int cx = int(SystemMetric(68)), cy = int(SystemMetric(69));
    const Gdi::RECT area{x - cx, y - cy, x + cx, y + cy};
    unsigned result = 0;
    while (state != Failed && state != Exited)
    {
        if (Api("GetCapture", nullptr) != hwnd)
            break;
        ProcessSentMessages();
        if (current_thread == InputThread())
            DrainInput();
        User::Message m{};
        if (User::PeekMessage(&m, 0, 0x200, 0x20a, true) || User::PeekMessage(&m, 0, 0x23, 0x23, true) ||
            User::PeekMessage(&m, 0, 0x100, 0x108, true))
        {
            if (m.message == 0x202)
                break;
            if (m.message == 0x100 && m.wparam == 0x1b)
                break;
            if (m.message == 0x200 && (m.x < area.left || m.x >= area.right || m.y < area.top || m.y >= area.bottom))
            {
                result = 1;
                break;
            }
            continue;
        }
        Suspend(Idle);
    }
    if (Api("GetCapture", nullptr) == hwnd)
        Api("ReleaseCapture", nullptr);
    return result;
}

// USER: drag an object from `from` over the children of `parent`.
// Every step asks the window under the pointer with WM_QUERYDROPOBJECT,
// tells the source with WM_DRAGLOOP and the sinks with WM_DRAGSELECT and
// WM_DRAGMOVE; the button coming up drops with WM_DROPOBJECT. The DROPSTRUCT
// lies in guest memory and goes to a 32-bit window procedure as a flat
// pointer.
unsigned Application::DragObject(unsigned parent, unsigned from, unsigned format, unsigned data,
                                 unsigned cursor)
{
    if (!User::IsWindow(parent) || (from && !User::IsWindow(from)) || (cursor && !FindIcon(cursor)))
        return 0;
    const unsigned drop = Allocate(0x24);
    if (!drop)
        return 0;
    unsigned char zero[0x24] = {};
    Store(drop, zero, sizeof zero);
    Write32(drop, from);
    Write32(drop + 8, format);
    Write32(drop + 12, data);
    const unsigned no = SystemCursor(Win98Cursor::No);
    unsigned shown = cursor ? cursor : no, sink = 0;
    const unsigned set[1] = {shown};
    const unsigned previous_cursor = Api("SetCursor", set);
    if (from)
    {
        const unsigned capture[1] = {from};
        Api("SetCapture", capture);
    }
    WinEventNotify(0x0e, from, 0, 0, true);
    bool dropped = false;
    while (state != Failed && state != Exited)
    {
        ProcessSentMessages();
        if (current_thread == InputThread())
            DrainInput();
        User::Message m{};
        if (!(User::PeekMessage(&m, 0, 0x200, 0x20a, true) || User::PeekMessage(&m, 0, 0xa0, 0xa9, true) ||
              User::PeekMessage(&m, 0, 0x100, 0x108, true)))
        {
            if (from && Api("GetCapture", nullptr) != from)
                break;
            Suspend(Idle);
            continue;
        }
        if (m.message == 0x100 && m.wparam == 0x1b)
        {
            shown = no;
            break;
        }
        if (m.message == 0x202 || m.message == 0xa2)
        {
            dropped = true;
            break;
        }
        // the deepest enabled, visible window under the point that
        // accepts the object.
        const unsigned point[2] = {unsigned(m.x), unsigned(m.y)};
        unsigned target = Api("WindowFromPoint", point);
        unsigned answer = 0;
        if (target)
        {
            Gdi::POINT client{m.x, m.y};
            User::ScreenToClient(target, &client);
            Write32(drop + 4, target);
            Write32(drop + 16, unsigned(client.x) & 0xffff | (unsigned(client.y) & 0xffff) << 16);
            answer = Send(target, 0x22b, 0, drop);
        }
        const unsigned next = answer ? target : 0;
        shown = !answer ? no : answer == 1 ? (cursor ? cursor : no) : answer;
        const unsigned change[1] = {shown};
        Api("SetCursor", change);
        if (from)
            Send(from, 0x22d, shown != no, drop);
        if (next != sink)
        {
            if (sink)
                Send(sink, 0x22e, 0, drop);
            if (next)
                Send(next, 0x22e, 1, drop);
            sink = next;
        }
        else if (sink)
            Send(sink, 0x22f, 0, drop);
    }
    if (from && Api("GetCapture", nullptr) == from)
        Api("ReleaseCapture", nullptr);
    const unsigned restore[1] = {previous_cursor};
    Api("SetCursor", restore);
    unsigned result = 0;
    if (dropped && shown != no && sink)
        result = Send(sink, 0x22a, from, drop);
    WinEventNotify(0x0f, from, 0, 0, true);
    Free(drop);
    return result;
}
} // namespace X86
