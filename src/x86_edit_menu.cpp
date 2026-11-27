// SPDX-License-Identifier: GPL-2.0-or-later
// The context menu of the EDIT control.
//
// USER answers WM_CONTEXTMENU of an edit control with a menu of its own:
// menu 3 of its resources, "Undo", "Cut", "Copy", "Paste", "Delete" and
// "Select All", whose identifiers are the messages they send (WM_UNDO 304h,
// WM_CUT 300h, WM_COPY 301h, WM_PASTE 302h, WM_CLEAR 303h and 401h, the
// EM_SETSEL of the 16-bit control). The routine in the edit segment of USER:
//
//   a control without the focus takes it first, and nothing opens when the
//   window is gone after that;
//   Undo is greyed without something to undo;
//   Cut and Delete are greyed in a read-only control, in one with a
//   password character and without a selection;
//   Paste is greyed in a read-only control and in one with a password
//   character, and where the clipboard holds no CF_TEXT;
//   Copy is greyed without a selection and with a password character;
//   Select All is greyed when the selection is the whole text;
//   a message from the keyboard (both coordinates -1) opens the menu in the
//   middle of the window rectangle;
//   TrackPopupMenuEx with TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
//   the menu is destroyed, and the choice is sent to the control as its
//   message, 401h with the range of the whole text.
//
// The branches of the Far East versions (the input method entries and the
// language callback of the edit record) are not part of this port.
#ifdef NEO98_RUNTIME_CONTENT
#include "windows_runtime_data.h"
#else
#include "../build/generated/user_menus.h"
#endif
#include "win98_localized_bytes.h"
#include "x86_app.h"
namespace X86
{
unsigned Application::EditContextMenu(unsigned hwnd, unsigned lp)
{
    if (User::GetFocus() != hwnd)
    {
        SetFocus(hwnd);
        if (!User::IsWindow(hwnd) || state == Failed || state == Exited)
            return 0;
    }
    unsigned position = 4;
    const unsigned root = ParseMenu(ResourceBytes(OriginalEditMenu), ResourceSize(OriginalEditMenu), position, 0);
    const unsigned menu = root ? User::GetSubMenu(root, 0) : 0;
    if (!menu)
    {
        if (root)
            User::DestroyMenu(root);
        return 0;
    }
    const unsigned style = User::Style(hwnd);
    unsigned start = 0, end = 0;
    User::EditSelection(hwnd, &start, &end);
    const unsigned length = User::TextLength(hwnd);
    const bool read_only = (style & 0x800u) != 0, password = User::EditMessage(hwnd, 0xd2, 0, 0) != 0;
    const bool selection = start != end;
    const auto grey = [&](unsigned item) { User::SetMenuFlags(menu, item, 3, false, 3); };
    if (!User::EditMessage(hwnd, 0xc6, 0, 0))
        grey(0x304);
    bool no_cut = false, no_paste = true;
    if (read_only || password)
        no_cut = true;
    else
    {
        no_cut = !selection;
        const unsigned text_format = 1;
        if (Api("IsClipboardFormatAvailable", &text_format))
            no_paste = false;
    }
    if (no_cut)
    {
        grey(0x300);
        grey(0x303);
    }
    if (no_paste)
        grey(0x302);
    if (!selection || password)
        grey(0x301);
    if (!start && end == length)
        grey(0x401);
    int x = short(lp & 0xffff), y = short(lp >> 16);
    if (lp == 0xffffffffu)
    {
        Gdi::RECT frame{};
        User::GetWindowRect(hwnd, &frame);
        x = frame.left + (frame.right - frame.left) / 2;
        y = frame.top + (frame.bottom - frame.top) / 2;
    }
    const unsigned track[6] = {menu, 0x182, unsigned(x), unsigned(y), hwnd, 0};
    const unsigned choice = Api("TrackPopupMenuEx", track);
    User::DestroyMenu(root);
    if (!choice || choice == 0xffffffffu || !User::IsWindow(hwnd) || state == Failed || state == Exited)
        return 0;
    // The 16-bit EM_SETSEL carries its range in lParam, 0 to FFFFh; the
    // message of a 32-bit window is EM_SETSEL with 0 and -1.
    if ((choice & 0xffff) == 0x401)
        return Send(hwnd, 0xb1, 0, 0xffffffffu);
    return Send(hwnd, choice & 0xffff, 0, 0);
}
} // namespace X86
