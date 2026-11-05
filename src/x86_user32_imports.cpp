// SPDX-License-Identifier: GPL-2.0-or-later
// USER32 exports used by guest programs, grouped by the state they share.
#include "x86_app.h"
#include "win98_broadcast.h"
#include <string.h>
namespace X86 {
namespace {
// Windows 98 has one desktop token, 0x12345678, shared by all threads.
// GetThreadDesktop returns it and SetThreadDesktop returns TRUE.
constexpr unsigned ThreadDesktopToken = 0x12345678;
// TRACKMOUSEEVENT flags. USER tests the query bit first, then the
// leave/hover bits at +4 and the cancel bit in the same DWORD.
constexpr unsigned TrackHover = 0x0001, TrackLeave = 0x0002;
constexpr unsigned TrackQuery = 0x40000000, TrackCancel = 0x80000000;
// BroadcastSystemMessage flags and recipients.
constexpr unsigned BroadcastQuery = 0x0001, BroadcastPostMessage = 0x0010;
constexpr unsigned RecipientApplications = 0x0008;
// keybd_event and mouse_event flag bits.
constexpr unsigned KeyEventExtended = 0x0001, KeyEventUp = 0x0002;
constexpr unsigned MouseMove = 0x0001, MouseLeftDown = 0x0002, MouseLeftUp = 0x0004;
constexpr unsigned MouseRightDown = 0x0008, MouseRightUp = 0x0010;
constexpr unsigned MouseMiddleDown = 0x0020, MouseMiddleUp = 0x0040;
constexpr unsigned MouseWheelFlag = 0x0800, MouseAbsolute = 0x8000;
// The action bits ChangeMenu reads at USER, the position bit
// it adds and the mask Windows 98 keeps for ModifyMenu.
constexpr unsigned MenuChange = 0x0080, MenuAppend = 0x0100, MenuDelete = 0x0200;
constexpr unsigned MenuByPosition = 0x0400, MenuSeparator = 0x0800, MenuRemove = 0x1000;
constexpr unsigned MenuChangeMask = 0x4c7f;
} // namespace

// USER32 CharToOemA, CharToOemBuffA, OemToCharA and OemToCharBuffA are two
// byte jumps into the import table: they forward to KERNEL32 ordinals 10, 11,
// 12 and 13, which NEO98 already reproduces in win98_kernel_ordinals_a.h.
unsigned Application::UserOemStringApi(ApiName name, const unsigned *a)
{
    if (API_IS(name, "CharToOemA")) return OemStringApi(10, a);
    if (API_IS(name, "CharToOemBuffA")) return OemStringApi(11, a);
    if (API_IS(name, "OemToCharA")) return OemStringApi(12, a);
    if (API_IS(name, "OemToCharBuffA")) return OemStringApi(13, a);
    Fail(name);
    return 0;
}

unsigned Application::User32ImportApi(ApiName name, const unsigned *a)
{
    switch (name.id)
    {
    // USER32!GetWindowWord and SetWindowWord (5632h) share the
    // worker of GetWindowLongA and SetWindowLongA with a width of two bytes.
    // Windows 98 sends negative indices through the ordinary window-long
    // thunk. Invalid handles and offsets preserve LastError on Win98.
    API_CASE("GetWindowWord")
    {
        unsigned value = 0;
        if (int(a[1]) < 0) return Api("GetWindowLongA", a) & 65535;
        User::WindowWord(a[0], int(a[1]), value, false);
        return value;
    }
    API_CASE("SetWindowWord")
    {
        unsigned value = a[2] & 65535;
        if (!User::WindowWord(a[0], int(a[1]), value, true)) return 0;
        return value;
    }
    // USER32!CopyIcon tails into CopyImage with IMAGE_ICON and
    // zero width, height and flags.
    API_CASE("CopyIcon")
    {
        const unsigned arguments[] = {a[0], 1, 0, 0, 0};
        return ImageApi("CopyImage", arguments);
    }
    // USER rejects two equal thread ids, a thread that owns no input
    // queue and a queue marked +14h bit 80h, and only then joins or splits the
    // two queues. NEO98 runs every GUI thread on the one native queue, so the
    // shared input state the caller asks for is already in place and neither
    // the join nor the split changes anything. Validation follows Windows 98 behavior.
    API_CASE("AttachThreadInput")
    {
        if (a[0] == a[1]) { last_error = 87; return 0; }
        unsigned found = 0;
        for (unsigned i = 0; i < ThreadCapacity; ++i)
        {
            if (threads[i].id && threads[i].id == a[0] && !threads[i].done) found |= 1;
            if (threads[i].id && threads[i].id == a[1] && !threads[i].done) found |= 2;
        }
        if (found != 3) { last_error = 1444; return 0; }
        return 1;
    }
    API_CASE("GetThreadDesktop") return ThreadDesktopToken;
    API_CASE("SetThreadDesktop") return 1;
    // USER32!SetWindowsHookA calls GetCurrentThreadId and then
    // SetWindowsHookExA with a null module and that thread.
    API_CASE("SetWindowsHookA")
    {
        const unsigned arguments[] = {a[0], a[1], 0, ThreadId()};
        return HookApi("SetWindowsHookExA", arguments);
    }
    // USER32!UnhookWindowsHook is the FT thunk of USER, the same worker
    // UnhookWindowsHookEx uses; the pair (type, procedure) selects the record.
    API_CASE("UnhookWindowsHook")
    {
        for (const auto &slot : hooks)
            if (slot.handle && slot.record.type == a[0] && slot.record.procedure == a[1])
            {
                const unsigned arguments[] = {slot.handle};
                return HookApi("UnhookWindowsHookEx", arguments);
            }
        last_error = 1404;
        return 0;
    }
    // USER32!GetAncestor is the word thunk of USER. GA_PARENT answers
    // the structural parent, GA_ROOT walks that tree, and GA_ROOTOWNER
    // follows GetParent's style-dependent parent/owner chain.
    API_CASE("GetAncestor")
    {
        if (!User::IsWindow(a[0])) { last_error = 1400; return 0; }
        const unsigned desktop = User::GetDesktopWindow();
        if (a[0] == desktop) return 0;
        if (a[1] < 1 || a[1] > 3) return 0;
        if (a[1] == 1) return User::Parent(a[0]) ? User::Parent(a[0]) : desktop;
        unsigned h = a[0];
        for (unsigned step = 0; step < User::WindowCapacity; ++step)
        {
            const auto node = User::WindowNode(h);
            unsigned next;
            if (a[1] == 3)
            {
                if (h == desktop) return h;
                const unsigned kind = node.style & 0xc0000000u;
                next = kind == 0x40000000u ? node.parent : kind & 0x80000000u ? node.owner : 0;
            }
            else
            {
                next = node.parent;
                if (next == desktop) return h;
            }
            if (!next) return h;
            h = next;
        }
        Fail("GetAncestor: the window tree does not terminate");
        return 0;
    }
    // USER32!GetWindowModuleFileNameA is the FT thunk of USER. The
    // answer is the module file name of the process that owns the window.
    API_CASE("GetWindowModuleFileNameA")
    {
        if (!User::IsWindow(a[0])) { last_error = 1400; return 0; }
        if (User::WindowProcess(a[0]) != process_id)
        {
            // The original reads the other process's module list. NEO98 keeps
            // no such list across processes, so this reports the failure
            // instead of the wrong file name.
            last_error = 5;
            return 0;
        }
        const unsigned arguments[] = {0, a[1], a[2]};
        return ModuleApi("GetModuleFileNameA", arguments);
    }
    // USER32!GetKeyboardType is the word thunk of the keyboard
    // driver entry. The native keyboard is the enhanced 101/102 key type 4
    // with subtype 0 and twelve function keys.
    API_CASE("GetKeyboardType")
    {
        if (a[0] == 0) return 4;
        if (a[0] == 1) return 0;
        if (a[0] == 2) return 12;
        last_error = 87;
        return 0;
    }
    // BroadcastSystemMessage selects recipients by component flags. Native
    // providers currently support application windows.
    API_CASE("BroadcastSystemMessage") API_CASE("BroadcastSystemMessageA")
    {
        unsigned recipients = RecipientApplications;
        if (a[1] && !Read(a[1], &recipients, 4)) { last_error = 87; return ~0u; }
        if (a[1] && recipients && !(recipients & RecipientApplications))
        {
            // BSM_VXDS, BSM_NETDRIVER and BSM_INSTALLABLEDRIVERS have no
            // native receiver; reporting success would be a lie.
            last_error = 120;
            return ~0u;
        }
        const unsigned mode = (a[0]&BroadcastQuery)?Win98Broadcast::Query:
            (a[0]&BroadcastPostMessage)?Win98Broadcast::Post:
            (a[0]&0x100)?Win98Broadcast::Notify:Win98Broadcast::Send;
        const unsigned result = BroadcastMessage(mode, a[2], a[3], a[4]);
        if (a[1]) Store(a[1], &recipients, 4);
        // A query succeeds unless a recipient returned BROADCAST_QUERY_DENY.
        // BSF_QUERY takes precedence over asynchronous delivery flags.
        return (a[0] & BroadcastQuery) ? result : 1u;
    }
    // USER32!keybd_event and mouse_event use USER thunks to hand events to the mouse and
    // keyboard drivers. NEO98 has one native input queue; the packet goes in
    // there the same way a host event does.
    API_CASE("keybd_event")
    {
        struct Injected { unsigned &depth; Injected(unsigned &d) : depth(d) { ++depth; } ~Injected() { --depth; } } injected(injected_input);
        const unsigned scan = a[1] & 255;
        unsigned character = 0;
        if (!(a[2] & KeyEventUp))
        {
            const unsigned arguments[] = {a[0] & 255, 2};
            character = KeyboardGameApi("MapVirtualKeyA", arguments) & 255;
        }
        RawKey(a[0] & 255, character, !(a[2] & KeyEventUp), scan, (a[2] & KeyEventExtended) != 0);
        return 0;
    }
    API_CASE("mouse_event")
    {
        struct Injected { unsigned &depth; Injected(unsigned &d) : depth(d) { ++depth; } ~Injected() { --depth; } } injected(injected_input);
        if (a[0] & MouseWheelFlag) { MouseScroll(int(a[3]), 0, 0); return 0; }
        Gdi::POINT cursor;
        User::CursorPosition(&cursor);
        int x = cursor.x, y = cursor.y;
        if (a[0] & MouseMove)
        {
            if (a[0] & MouseAbsolute) { x = int(a[1]) * 800 / 65536; y = int(a[2]) * 600 / 65536; }
            else { x += int(a[1]); y += int(a[2]); }
        }
        unsigned buttons = User::PhysicalMouseButtons();
        if (a[0] & MouseLeftDown) buttons |= 1;
        if (a[0] & MouseRightDown) buttons |= 2;
        if (a[0] & MouseMiddleDown) buttons |= 4;
        // A single event can contain both transitions. Deliver the press
        // before applying the release bits, as the Win98 driver does.
        MouseButtons(x, y, buttons);
        const unsigned pressed=buttons;
        if (a[0] & MouseLeftUp) buttons &= ~1u;
        if (a[0] & MouseRightUp) buttons &= ~2u;
        if (a[0] & MouseMiddleUp) buttons &= ~4u;
        if(buttons!=pressed)MouseButtons(x, y, buttons);
        return 0;
    }
    // USER validates the pointer and Windows 98 reads cbSize at +0,
    // dwFlags at +4, hwndTrack at +8 and dwHoverTime at +0Ah.
    API_CASE("TrackMouseEvent")
    {
        unsigned record[4] = {};
        if (!Read(a[0], record, sizeof record) || record[0] != sizeof record)
        {
            last_error = 87;
            return 0;
        }
        if (record[1] & TrackQuery)
        {
            // Windows 98 answers with the tracking that is in place.
            User::QueryMouseTrack(record[1],record[2],record[3]);
            return Store(a[0], record, sizeof record) ? 1u : 0u;
        }
        if (!User::IsWindow(record[2])) { last_error = 1400; return 0; }
        if (!(record[1] & (TrackLeave|TrackHover))||
            (record[1]&~(TrackLeave|TrackHover|TrackCancel))) { last_error = 87; return 0; }
        return User::TrackMouse(record[2],record[1],record[3])?1u:0u;
    }
    // USER32!GetDlgItemInt reads the control's text and parses it.
    // pushes the ANSI marker, so the shared worker is the one
    // SetDlgItemInt formats for.
    API_CASE("GetDlgItemInt")
    {
        char text[64] = {};
        const unsigned control = User::GetDlgItem(a[0], a[1]);
        bool okay = control != 0;
        if (okay) User::GetWindowText(control, text, sizeof text);
        unsigned value = 0;
        const char *p = text;
        while (*p == ' ' || *p == '\t') ++p;
        const bool negative = a[3] && *p == '-';
        if (*p == '+' || (a[3] && *p == '-')) ++p;
        if (!*p) okay = false;
        for (; *p; ++p)
        {
            if (*p < '0' || *p > '9') { okay = false; break; }
            const unsigned digit = unsigned(*p - '0');
            if (value > (0xffffffffu - digit) / 10) { okay = false; break; }
            value = value * 10 + digit;
        }
        if (okay && a[3] && value > (negative ? 0x80000000u : 0x7fffffffu)) okay = false;
        if (okay && negative) value = unsigned(-int(value));
        if (!okay) value = 0;
        if (a[2])
        {
            const unsigned flag = okay ? 1u : 0u;
            if (!Store(a[2], &flag, 4)) return 0;
        }
        return value;
    }
    // USER32!AlignRects goes over to the shim, which copies the rectangles into words and widens them back with
    // sign, and on to the body USER (arguments rects, count, primary,
    // flags; the primary index is never read). More than nine rectangles answer
    // FALSE. One rectangle skips all alignment and is only
    // moved so that its top left corner lies at zero.
    // Several rectangles are snapped to eighths and run through further
    // alignment passes, which are not implemented; the port has one
    // display, so it stops there with a diagnosis. The display control panel
    // calls it for its monitor layout.
    API_CASE("AlignRects")
    {
        const unsigned count = a[1];
        if (count > 9) return 0;
        if (count > 1)
        {
            Fail("AlignRects with several rectangles: USER and are not implemented");
            return 0;
        }
        if (!count) return 1;
        int edges[4];
        if (!Read(a[0], edges, sizeof edges)) { last_error = 87; return 0; }
        const short left = short(edges[0]), top = short(edges[1]);
        for (unsigned i = 0; i < 4; ++i) edges[i] = short(short(edges[i]) - (i & 1 ? top : left));
        return Store(a[0], edges, sizeof edges) ? 1u : 0u;
    }
    // USER32!ChangeMenuA goes over the third thunk table into
    // USER. That entry runs no menu logic at all: it rewrites its five
    // arguments into one of the four calls that do and jumps there.
    API_CASE("ChangeMenuA")
    {
        // without a new item the call means a separator.
        unsigned flags = a[4] | (a[2] ? 0u : MenuSeparator);
        if (flags & MenuRemove)
        {
            // Windows 98 clears MF_REMOVE and adds MF_BYPOSITION, so this form
            // always names the item by its place.
            const unsigned arguments[] = {a[0], a[1], (flags & ~MenuRemove) | MenuByPosition};
            return Api("RemoveMenu", arguments);
        }
        if (flags & MenuDelete)
        {
            const unsigned arguments[] = {a[0], a[1], flags & ~MenuDelete}; //
            return Api("DeleteMenu", arguments);
        }
        if (flags & MenuChange)
        {
            // Windows 98 keeps only the mask 4C7Fh, which drops the other action
            // bits and the two USER keeps for itself.
            const unsigned arguments[] = {a[0], a[1], flags & MenuChangeMask, a[3], a[2]};
            return Api("ModifyMenuA", arguments);
        }
        if (flags & MenuAppend)
        {
            // throws the position away and inserts at the end.
            const unsigned arguments[] = {a[0], ~0u, (flags & ~MenuAppend) | MenuByPosition, a[3], a[2]};
            return Api("InsertMenuA", arguments);
        }
        const unsigned arguments[] = {a[0], a[1], flags, a[3], a[2]}; //
        return Api("InsertMenuA", arguments);
    }
    // USER32!LoadMenuIndirectA uses the USER implementation. It reads a version word and a header length, skips the
    // header and hands the rest to the same two readers LoadMenu uses:
    // for the classic MENUITEMTEMPLATE chain and for the extended one.
    // A version other than zero or one answers null.
    API_CASE("LoadMenuIndirectA")
    {
        if (!a[0]) { last_error = 87; return 0; }
        // The template names no length. Read the readable pages behind it, at
        // most one MiB, exactly as the indirect dialog does.
        constexpr unsigned limit = 1024 * 1024;
        unsigned length = 0;
        while (length < limit && a[0] <= ~0u - length)
        {
            const unsigned at = a[0] + length;
            VirtualMemory::Info info{};
            if (!virtual_memory.Query(at, info) || info.base > at || info.size <= at - info.base) break;
            unsigned span = info.size - (at - info.base);
            if (span > limit - length) span = limit - length;
            if (span > ~0u - at || !virtual_memory.Access(at, span, false)) break;
            length += span;
        }
        if (length < 4) { last_error = 87; return 0; }
        auto *bytes = new unsigned char[length];
        if (!bytes) { last_error = 8; return 0; }
        unsigned menu = 0;
        if (Read(a[0], bytes, length))
        {
            const auto word = [](const unsigned char *p) { return p[0] | unsigned(p[1]) << 8; };
            const unsigned version = word(bytes), header = word(bytes + 2);
            if (version <= 1 && (version == 0 || (length >= 8 && header >= 4)))
            {
                unsigned position = 4 + header;
                if (position <= length) menu = ParseMenu(bytes, length, position, 0, version == 1);
            }
        }
        delete[] bytes;
        return menu;
    }
    // USER32!RegisterDeviceNotificationA is full 32-bit code; no
    // thunk connection reaches USER.EXE. to Windows 98 reject a null
    // recipient, a null or unreadable filter, a recipient that is not a window
    // and any flag bit, all with ERROR_INVALID_PARAMETER. then reads
    // dbch_devicetype: every value but 5 and 6 answers ERROR_INVALID_DATA.
    API_CASE("RegisterDeviceNotificationA")
    {
        unsigned filter[3] = {};
        if (!a[0] || !a[1] || !Read(a[1], filter, sizeof filter) ||
            !User::IsWindow(a[0]) || a[2])
        {
            last_error = 87;
            return 0;
        }
        if (filter[1] == 6)
        {
            // Windows 98 handles DBT_DEVTYP_HANDLE through a wrapper around
            // NTDLL NtGetDevnodeFromFileHandle. Win98 answers
            // ERROR_INVALID_DATA whenever that lookup fails, and NEO98 keeps no
            // device nodes at all, so it always does.
            last_error = 13;
            return 0;
        }
        if (filter[1] != 5 || filter[0] < 0x1d) { last_error = 13; return 0; }
        // Windows 98 keeps a 24h byte record: the marker 07625109h at +8, the
        // window at +0Ch, the flags at +10h and the interface class at +14h.
        unsigned record[9] = {};
        record[2] = 0x07625109;
        record[3] = a[0];
        record[4] = a[2];
        if (!Read(a[1] + 12, record + 5, 16)) { last_error = 13; return 0; }
        const unsigned block = Allocate(sizeof record);
        if (!block) { last_error = 8; return 0; }
        if (!Store(block, record, sizeof record)) { Free(block); return 0; }
        return block;
    }
    // USER32!UnregisterDeviceNotification reads the 24h byte record
    // back, checks the marker at +8 and answers ERROR_INVALID_DATA for a
    // foreign one; a null or unreadable handle is ERROR_INVALID_PARAMETER.
    API_CASE("UnregisterDeviceNotification")
    {
        unsigned record[9] = {};
        if (!a[0] || !Read(a[0], record, sizeof record)) { last_error = 87; return 0; }
        if (record[2] != 0x07625109 && record[2] != 0x17625109) { last_error = 13; return 0; }
        return Free(a[0]) ? 1u : 0u;
    }
    default:
        break;
    }
    Fail(name);
    return 0;
}
} // namespace X86
