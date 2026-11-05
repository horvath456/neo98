// SPDX-License-Identifier: GPL-2.0-or-later
// USER32 window exports that the import rounds left out: AnimateWindow,
// later in this file, the system colours, the two temporary drawing entries
// and the MDI, dialog and property thunks. All of them are word thunks into
// USER.EXE; the route and the 16-bit target of each one is named above its
// handler.
#include "user.h"
#include "x86_app.h"
#include "win98_frame_control.h"
#include <string.h>
namespace X86 {
namespace {
// AnimateWindow's flag words.
constexpr unsigned AnimateDirections = 0x0000001F; // AW_HOR_*, AW_VER_*, AW_CENTER
constexpr unsigned AnimateHide = 0x00010000, AnimateActivate = 0x00020000;
constexpr unsigned AnimateBlend = 0x00080000;
constexpr unsigned AnimateKnown = 0x000F001F;      // what lets pass without a log
constexpr unsigned AnimateWorks = AnimateDirections | AnimateBlend; // masks with this
// SetWindowPos flags., and Windows 98 build these three words.
constexpr unsigned PosShow = 0x425F, PosHide = 0x028F, PosActivate = 0x0207;
constexpr unsigned PosNoActivate = 0x0010;
} // namespace

// USER32, CL B6h through the flat-thunk table at USER, entry, so the body is USER over the worker.
// masks the flags against 000F001Fh and hands a stray bit to
// KERNEL.327 LogParamError with ERR_BAD_FLAGS (F005h); the retail KERNEL does
// nothing with it and the worker runs on, so a stray bit is not an error.
// The worker animates into a window region and blits it frame by frame. NEO98
// draws no animation: this reproduces the decisions and the end state, which
// is what the caller can observe once the last frame is gone.
unsigned Application::AnimateWindow(unsigned hwnd, unsigned time, unsigned flags)
{
    (void)time;  // measures the frames against it; there are none here.
    // without a direction and without AW_BLEND there is nothing to do.
    if (!(flags & AnimateWorks)) return 0;
    const bool hide = (flags & AnimateHide) != 0;
    if (flags & AnimateBlend)
    {
    // Win98 has no layered windows; the blend cannot fade out.
        if (hide) return 0;
    }
    // Windows 98 reads the window region word at WND+54h and gives up when the
    // window already has one, because the animation needs that field itself.
    else if (User::WindowRegion(hwnd)) return 0;
    if (!User::IsWindow(hwnd)) { last_error = 1400; return 0; }
    // over IsWindowVisible. Showing an already visible window
    // and hiding an already hidden one both fail.
    if (User::IsVisible(hwnd) != hide) return 0;
    if (!hide)
    {
        // the window becomes visible before the first frame, without
        // a repaint and without taking the activation.
        const unsigned show[] = {hwnd, 0, 0, 0, 0, 0, PosShow};
        Api("SetWindowPos", show);
    }
    if (!User::IsWindow(hwnd)) return 0;
    if (hide)
    {
        // the last step of a hide is SWP_HIDEWINDOW, and the window
        // keeps the activation unless the caller asked for AW_ACTIVATE.
        const unsigned off[] = {hwnd, 0, 0, 0, 0, 0,
                                PosHide | ((flags & AnimateActivate) ? 0u : PosNoActivate)};
        Api("SetWindowPos", off);
    }
    else if (flags & AnimateActivate)
    {
        // a shown window takes the activation at the end.
        const unsigned on[] = {hwnd, 0, 0, 0, 0, 0, PosActivate};
        Api("SetWindowPos", on);
    }
    // whatever happened, the animation region goes away again.
    if (User::IsWindow(hwnd)) User::SetWindowRegion(hwnd, 0, false);
    return 1;
}

namespace {
// A mode change moves the shown system colours; their brushes follow in place.
struct SystemColourFollower {
    SystemColourFollower()
    {
        User::SetSystemColourListener(Application::FollowSystemColour);
    }
} system_colour_follower;
}
void Application::FollowSystemColour(unsigned index, unsigned colour)
{
    if (index < 32 && system_brushes[index]) Gdi::SetSolidBrushColour(system_brushes[index], colour);
}
// USER SetOneSysColor for one index of one call.
void Application::SetSystemColour(unsigned index, unsigned colour)
{
    // The table holds 00RRGGBB, the caller passes a COLORREF 00BBGGRR.
    Win98Frame::SetSystemColor(index, ((colour & 0xff) << 16) | (colour & 0xff00) |
                                          ((colour >> 16) & 0xff));
    // an existing brush is recoloured, never replaced. Every handle
    // GetSysColorBrush handed out keeps working, as in the original.
    if (index < 32 && system_brushes[index]) Gdi::SetSolidBrushColour(system_brushes[index], colour);
}

unsigned Application::WindowExportApi(ApiName name, const unsigned *a)
{
    switch (name.id)
    {
    API_CASE("AnimateWindow")
    {
        // Log unknown flag bits, kept as a diagnosis rather than as an
        // error: the retail KERNEL drops it, and so does the port.
        (void)AnimateKnown;
        return AnimateWindow(a[0], a[1], a[2]);
    }
    // USER32 over USER and its body. Two passes over
    // the caller's arrays: the four colours the 3-D shades come from first,
    // then everything else, then one broadcast and one repaint of the whole
    // desktop. It writes nothing to the registry and nothing to WIN.INI; the
    // control panel does that itself.
    API_CASE("SetSysColors")
    {
        // Win98 rejects a negative count without reading the arrays. Narrowing
        // -1 first would turn it into 65535 entries and overrun both buffers.
        if (int(a[0]) < 0) return 0;
        const unsigned count = a[0] & 0xffffu;   // pushes a word
        if (count && (!a[1] || !a[2])) { last_error = 87; return 0; }
        for (unsigned pass = 0; pass < 2; ++pass)
            for (unsigned i = 0; i < count; ++i)
            {
                unsigned index = 0, colour = 0;
                if (!Read(a[1] + i * 4, &index, 4) || !Read(a[2] + i * 4, &colour, 4)) return 0;
                const int slot = short(index & 0xffffu);   // the word thunk narrows
                // an index above 28 is dropped. The original lets a
                // negative index through and writes below the table; NEO98
                // drops it instead of corrupting its own data.
                if (slot < 0 || slot > 28) continue;
                const bool source = slot == 15 || slot == 16 || slot == 20 || slot == 25;
                if (source != (pass == 0)) continue;
                SetSystemColour(unsigned(slot), colour);
            }
        // over : WM_SYSCOLORCHANGE with both parameters zero to
        // every top-level window, and only to those.
        unsigned windows[User::WindowCapacity], live = 0;
        if (User::WindowSnapshot(0, windows, User::WindowCapacity, live))
            for (unsigned i = 0; i < live && state != Failed && state != Exited; ++i)
                if (User::IsWindow(windows[i])) Send(windows[i], 0x15, 0, 0);
        // over : the whole desktop tree, frames and backgrounds.
        User::RedrawWindow(User::GetDesktopWindow(), nullptr, 0, 0x485);
        // Windows 98 answers TRUE whatever happened.
        return 1;
    }
    // USER32 over USER. Swaps the colour and the brush table
    // wholesale and hands back a cookie that puts the old ones back. It sends
    // nothing, repaints nothing and creates no brush.
    API_CASE("SetSysColorsTemp")
    {
        if (!a[0])
        {
            // the restore form. The cookie names a saved pair.
            for (auto &saved : system_colour_saves)
            {
                if (!saved.used || saved.cookie != a[2]) continue;
                for (unsigned i = 0; i < saved.count; ++i)
                {
                    Win98Frame::SetSystemColor(i, saved.colours[i]);
                    if (i < 32) system_brushes[i] = saved.brushes[i];
                }
                saved = SystemColourSave{};
                return 1;
            }
            return 0;
        }
        // at most the 29 indices the table has.
        if (a[2] > 29) return 0;
        SystemColourSave *slot = nullptr;
        for (auto &saved : system_colour_saves) if (!saved.used) { slot = &saved; break; }
        if (!slot) { last_error = 8; return 0; }
        const unsigned *table = Win98Frame::SystemColors();
        slot->count = a[2];
        for (unsigned i = 0; i < a[2]; ++i)
        {
            slot->colours[i] = table[i];
            slot->brushes[i] = i < 32 ? system_brushes[i] : 0;
            unsigned colour = 0, brush = 0;
            if (!Read(a[0] + i * 4, &colour, 4) || !Read(a[1] + i * 4, &brush, 4)) return 0;
            Win98Frame::SetSystemColor(i, ((colour & 0xff) << 16) | (colour & 0xff00) |
                                              ((colour >> 16) & 0xff));
            if (i < 32) system_brushes[i] = brush;
        }
        slot->used = true;
        slot->cookie = SystemColourCookie + unsigned(slot - system_colour_saves);
        return slot->cookie;
    }
    // USER32 over USER and its body. One item changes
    // its MF_HILITE bit and is redrawn where it stands; nothing is sent.
    API_CASE("HiliteMenuItem")
    {
        // refuses a flag word with 1000h, 2000h or 8000h set.
        if (a[3] & 0xb000) return 0;
        if (!User::IsWindow(a[0]) || !User::IsMenu(a[1])) return 0;
        const bool by_position = (a[3] & 0x400) != 0;
        // turns an identifier into a position and does not check the
        // answer; an item that is not there simply changes nothing.
        User::SetMenuFlags(a[1], a[2], (a[3] & 0x80) ? 0x80u : 0u, by_position, 0x80u);
        User::DrawMenuBar(a[0]);
        // Windows 98 answers TRUE whatever happened.
        return 1;
    }
    // USER32 is pure stack work: it puts WS_EX_MDICHILD in front of
    // the arguments and a null menu between the parent and the instance, then
    // jumps into CreateWindowExA.
    API_CASE("CreateMDIWindowA")
    {
        const unsigned create[] = {0x40, a[0], a[1], a[2], a[3], a[4], a[5], a[6],
                                   a[7], 0, a[8], a[9]};
        return Api("CreateWindowExA", create);
    }
        // Both take the selected line of a list box apart; the combo
    // form looks the list up behind the combo box first. Either way the
    // caller's buffer takes at most 128 bytes including the terminator,
    // whatever length it offered.
    API_CASE("DlgDirSelectExA") API_CASE("DlgDirSelectComboBoxExA")
    {
        const bool combo = API_IS(name, "DlgDirSelectComboBoxExA");
        if (!a[1] || !a[2]) { last_error = 87; return 0; }
        const unsigned control = User::GetDlgItem(a[0], a[3]);
        if (!control) return 0;
        unsigned list = control;
        if (combo)
        {
            unsigned view[12] = {};
            // a combo box without its own record answers FALSE.
            if (!ComboBoxView(control, view) || !view[11]) return 0;
            list = view[11];
        }
        char text[128] = {};
        const bool directory = DialogDirectorySelection(list, text, sizeof text);
        unsigned room = a[2] < sizeof text ? a[2] : unsigned(sizeof text);
        if (!room) return directory;
        unsigned length = unsigned(strlen(text));
        if (length >= room) length = room - 1;
        text[length] = 0;
        if (!Store(a[1], text, length + 1)) return 0;
        return directory ? 1 : 0;
    }
    // USER32 (CL 2Ah) and (CL 29h) over USER and its
    // body.
    API_CASE("EnumPropsA") return EnumProperties(a[0], a[1], 0, false);
    API_CASE("EnumPropsExA") return EnumProperties(a[0], a[1], a[2], true);
    default:
        break;
    }
    Fail(name);
    return 0;
}
} // namespace X86
