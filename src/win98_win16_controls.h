// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98Win16Controls
{
// USER.EXE dispatches 0401..042E as the listbox messages
// corresponding to USER32 0180..01AD. 0400 and 042F take DefWindowProc.
// The window's native control identity, including a subclassed listbox,
// must be checked by the caller before using this table.
inline unsigned ListTo32(unsigned message)
{
    return message >= 0x401 && message <= 0x42e ? message - 0x281 : message;
}
inline unsigned ListTo16(unsigned message)
{
    return message >= 0x180 && message <= 0x1ad ? message + 0x281 : message;
}
enum Buffer { None, StringIn, TextOut, RectIn, RectOut, WordsIn, WordsOut, ImageAnswer };
// USER: STATIC numbers STM_SETICON, STM_GETICON, STM_SETIMAGE
// and STM_GETIMAGE 0400h to 0403h; USER32 names the same bodies 0170h to 0173h.
inline unsigned StaticTo32(unsigned message)
{
    return message >= 0x400 && message <= 0x403 ? message - 0x290 : message;
}
inline unsigned ListBuffer(unsigned message, unsigned style)
{
    const bool owner = (style & 0x10) || ((style & 0x20) && !(style & 0x210));
    const bool strings = !owner || (style & 0x40);
    switch (message)
    {
    case 0x180: case 0x181: case 0x18c: case 0x18f: case 0x1a2:
        return strings ? StringIn : None;
    case 0x18d: case 0x196: return StringIn;
    case 0x189: return TextOut;
    case 0x191: return WordsOut;
    case 0x192: return WordsIn;
    case 0x198: return RectOut;
    default: return None;
    }
}
inline unsigned SignedWord(unsigned word) { return (unsigned)(int)(short)word; }
inline void ListParams32(unsigned message, unsigned &wp, unsigned &lp)
{
    // A WORD index of FFFF means -1 (append, search from the start, or all
    // items). Item data and packed index pairs remain DWORDs.
    switch (message)
    {
    case 0x181: case 0x182: case 0x183: case 0x186: case 0x187:
    case 0x189: case 0x18a: case 0x18c: case 0x18f: case 0x198:
    case 0x199: case 0x19a: case 0x19c: case 0x19e: case 0x1a2:
        wp = SignedWord(wp); break;
    }
    if (message == 0x183 || message == 0x185)
        lp = SignedWord(lp);
}
// The window kind a 16-bit BUTTON gets in To32/To16; the other numbers are
// the edit (2) and the common-control numbers of User::CommonControl.
constexpr unsigned ButtonControl = 12;
// Win16 numbers BM_GETCHECK, BM_SETCHECK, BM_GETSTATE, BM_SETSTATE and
// BM_SETSTYLE WM_USER+0 to WM_USER+4; USER32 names the same bodies 00F0h to
// 00F4h. A WinZip self-extractor asks its checkbox with SendDlgItemMessage
// 0400h and got the answer of a message the button did not know.
inline unsigned ButtonTo32(unsigned message) { return message >= 0x400 && message <= 0x404 ? message - 0x310 : message; }
inline unsigned ButtonTo16(unsigned message) { return message >= 0xf0 && message <= 0xf4 ? message + 0x310 : message; }
// USER dispatches 0400..0421. 0422/0423 are holes.
inline unsigned To32(unsigned control, unsigned message)
{
    if (control == ButtonControl) return ButtonTo32(message);
    if (control == 2)
        return message >= 0x400 && message <= 0x422 && message != 0x40f && message != 0x410 && message != 0x41a
                   ? message - 0x350 : message;
    if (control == 4) return ListTo32(message);
    return control == 6 && message >= 0x400 && message <= 0x421 ? message - 0x2c0 : message;
}
inline unsigned To16(unsigned control, unsigned message)
{
    if (control == ButtonControl) return ButtonTo16(message);
    if (control == 2)
        return message >= 0xb0 && message <= 0xd2 && message != 0xbf && message != 0xc0 && message != 0xca
                   ? message + 0x350 : message;
    if (control == 4) return ListTo16(message);
    return control == 6 && message >= 0x140 && message <= 0x161 ? message + 0x2c0 : message;
}
inline bool HasStrings(unsigned control, unsigned style)
{
    if (control == 6) return !(style & 0x30) || (style & 0x200);
    const bool owner = (style & 0x10) || ((style & 0x20) && !(style & 0x210));
    return !owner || (style & 0x40);
}
inline unsigned MessageBuffer(unsigned control, unsigned message, unsigned style)
{
    if (control == 4) return ListBuffer(message, style);
    if (control == 2)
    {
        switch (message)
        {
        case 0xb2: return RectOut;
        case 0xb3: case 0xb4: return RectIn;
        case 0xc2: return StringIn;
        case 0xc4: return TextOut;
        case 0xcb: return WordsIn;
        default: return None;
        }
    }
    if (control != 6) return None;
    switch (message)
    {
    case 0x143: case 0x14a: case 0x14c: case 0x14d: case 0x158:
        return HasStrings(control, style) ? StringIn : None;
    case 0x145: return StringIn;
    case 0x148: return TextOut;
    case 0x152: return RectOut;
    default: return None;
    }
}
inline void Params32(unsigned control, unsigned message, unsigned &wp, unsigned &lp)
{
    if (control == 4) ListParams32(message, wp, lp);
    else if (control == 2)
    {
        if (message == 0xb0) { wp = 0; lp = 0; }
        else if (message == 0xb1)
        {
            const unsigned packed = lp;
            wp = SignedWord(packed);
            lp = SignedWord(packed >> 16);
        }
        switch (message)
        {
        case 0xbb: case 0xc1: case 0xc9: wp = SignedWord(wp); break;
        }
        // Win16 WPARAM is a WORD, while native EM_LINESCROLL takes a signed
        // int. Keep this narrowing at the wire boundary; native callers may
        // use the complete 32-bit range.
        if (message == 0xb6) wp = SignedWord(wp);
    }
    else if (control == 6)
    {
        if (message == 0x140) { wp = 0; lp = 0; } // Win16 only returns the packed selection.
        switch (message)
        {
        case 0x144: case 0x148: case 0x149: case 0x14a: case 0x14c: case 0x14d:
        case 0x14e: case 0x150: case 0x151: case 0x153: case 0x154: case 0x158:
            wp = SignedWord(wp); break;
        }
    }
}
} // namespace Win98Win16Controls
