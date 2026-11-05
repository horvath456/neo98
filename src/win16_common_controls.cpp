// SPDX-License-Identifier: GPL-2.0-or-later
// The image lists and the tree view of the 16-bit COMMCTRL.DLL for 16-bit
// callers such as SYSDM.CPL's page "Device Manager".
//
// COMMCTRL.DLL is ours by the rule of PLAN.md section 0. In Win98 it carries
// its own 16-bit image lists (ordinals 40 to 54) and its own tree view
// (TV_WNDPROC, class "SysTreeView"). This port answers both with its native
// counterparts, the image lists of src/x86_image.cpp and the tree view of
// src/x86_tree.cpp, and translates at the edge what is 16-bit about them:
//
// - HIMAGELIST is a word on the 16-bit side (ImageList_Destroy ends in
// `retf 2`); the words here name native image lists.
// - TV_ITEM has the packed 16-bit shape of COMMCTRL (mask word,
// hItem dword at +2, state and stateMask words, pszText far pointer at
// +0Ah, cchTextMax, iImage, iSelectedImage and cChildren words, lParam
// dword at +16h; 26 bytes). TV_INSERTSTRUCT puts it behind hParent and
// hInsertAfter. HTREEITEM is a dword on both sides.
// - The notifications keep the NMHDR of 12 bytes (COMMCTRL, see
// src/win16_property_sheet.cpp) and then the packed 16-bit structures:
// NM_TREEVIEW of (action word at +0Ch, itemOld at +0Eh, itemNew
// at +28h, ptDrag at +42h, 46h bytes), TV_DISPINFO (item at +0Ch) and
// TV_KEYDOWN (wVKey and flags words).
#include "win16_machine.h"
#include "x86_app.h"
#include "win_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
namespace X86
{
namespace
{
constexpr unsigned ImageListWordBase = 0x4c01, ImageListWords = 32;
constexpr unsigned TvifText = 1;
constexpr unsigned TextCallback = 0xffffffffu;
unsigned Signed(unsigned word) { return unsigned(int(short(word))); }
} // namespace

unsigned Application::Win16ImageListNative(unsigned word)
{
    word &= 0xffff;
    if (word < ImageListWordBase || word >= ImageListWordBase + ImageListWords)
        return 0;
    return win16_image_lists[word - ImageListWordBase];
}

unsigned Application::Win16ImageListWord(unsigned native)
{
    if (!native)
        return 0;
    for (unsigned i = 0; i < ImageListWords; ++i)
        if (win16_image_lists[i] == native)
            return ImageListWordBase + i;
    for (unsigned i = 0; i < ImageListWords; ++i)
        if (!win16_image_lists[i] || !FindImageList(win16_image_lists[i]))
        {
            win16_image_lists[i] = native;
            return ImageListWordBase + i;
        }
    return 0;
}

// COMMCTRL 40 to 54. The argument sizes are the `retf` counts of the 16-bit
// bodies: Create 0Ah, Destroy 2, GetImageCount 2, SetOverlayImage 6,
// SetBkColor 6, GetBkColor 2, Add 6, Draw 0Ch, AddIcon 4, ReplaceIcon 6.
bool Application::Win16ImageListCall(unsigned ordinal, const Win16Frame &frame, unsigned &result, unsigned &words)
{
    result = 0;
    switch (ordinal)
    {
    case 40: // ImageList_Create(cx, cy, flags, cInitial, cGrow)
    {
        words = 5;
        const unsigned a[] = {frame.Word(4), frame.Word(3), frame.Word(2) & 0x13f, frame.Word(1), frame.Word(0)};
        result = Win16ImageListWord(ImageApi("ImageList_Create", a));
        return true;
    }
    case 41: // ImageList_Destroy(himl)
    {
        words = 1;
        const unsigned native = Win16ImageListNative(frame.Word(0));
        if (!native)
            return true;
        const unsigned a[] = {native};
        result = ImageApi("ImageList_Destroy", a);
        win16_image_lists[(frame.Word(0) & 0xffff) - ImageListWordBase] = 0;
        return true;
    }
    case 42: // ImageList_GetImageCount(himl)
    {
        words = 1;
        const unsigned a[] = {Win16ImageListNative(frame.Word(0))};
        result = a[0] ? ImageApi("ImageList_GetImageCount", a) : 0;
        return true;
    }
    case 43: // ImageList_SetOverlayImage(himl, iImage, iOverlay)
    {
        // The native image list draws no overlays; the device list shows its
        // problem marks through them. The answer is the one of a valid call.
        words = 3;
        result = Win16ImageListNative(frame.Word(2)) ? 1 : 0;
        return true;
    }
    case 44: // ImageList_SetBkColor(himl, color)
    {
        words = 3;
        const unsigned a[] = {Win16ImageListNative(frame.Word(2)), frame.Long(0)};
        result = a[0] ? ImageApi("ImageList_SetBkColor", a) : ~0u;
        return true;
    }
    case 45: // ImageList_GetBkColor(himl)
    {
        words = 1;
        auto *list = FindImageList(Win16ImageListNative(frame.Word(0)));
        result = list ? list->background : ~0u;
        return true;
    }
    case 46: // ImageList_Add(himl, hbmImage, hbmMask)
    {
        // The strip is cut into images of the list's width. Where the mask is
        // white the image is transparent; its colour there goes to black, as
        // the drawing with SRCAND and SRCINVERT needs.
        words = 3;
        result = 0xffff;
        auto *list = FindImageList(Win16ImageListNative(frame.Word(2)));
        const unsigned image = frame.Word(1), mask = frame.Word(0);
        unsigned info[6] = {};
        if (!list || !image || !Gdi::BitmapInfo(image, info) || !list->width || !list->height)
            return true;
        const unsigned count = info[1] / list->width, first = list->count;
        const auto dc = Gdi::CreateCompatibleDC(0), mask_dc = mask ? Gdi::CreateCompatibleDC(0) : 0;
        if (!dc || (mask && !mask_dc))
        {
            if (dc) Gdi::DeleteDC(dc);
            if (mask_dc) Gdi::DeleteDC(mask_dc);
            return true;
        }
        const auto keep = Gdi::SelectObject(dc, image), keep_mask = mask ? Gdi::SelectObject(mask_dc, mask) : 0;
        const unsigned width = list->width, height = list->height < info[2] ? list->height : info[2];
        for (unsigned n = 0; n < count && list->count < 128; ++n)
        {
            auto *item = new WinImage::Image;
            if (!item)
                break;
            item->color = static_cast<unsigned *>(malloc(width * list->height * 4));
            item->mask = static_cast<unsigned *>(malloc(width * list->height * 4));
            if (!item->color || !item->mask)
            {
                delete item;
                break;
            }
            item->width = width;
            item->height = list->height;
            for (unsigned y = 0; y < list->height; ++y)
                for (unsigned x = 0; x < width; ++x)
                {
                    const unsigned at = y * width + x;
                    const bool inside = y < height;
                    const unsigned c = inside ? Gdi::GetPixel(dc, int(n * width + x), int(y)) : 0;
                    const bool clear = !inside || (mask && (Gdi::GetPixel(mask_dc, int(n * width + x), int(y)) & 0xffffff));
                    item->mask[at] = clear ? 0xffffffffu : 0;
                    item->color[at] = clear ? 0 : ((c & 0xff) << 16) | (c & 0xff00) | ((c >> 16) & 0xff);
                }
            list->items[list->count++] = item;
        }
        Gdi::SelectObject(dc, keep);
        Gdi::DeleteDC(dc);
        if (mask_dc)
        {
            Gdi::SelectObject(mask_dc, keep_mask);
            Gdi::DeleteDC(mask_dc);
        }
        result = list->count > first ? first : 0xffff;
        return true;
    }
    case 49: // ImageList_Draw(himl, i, hdc, x, y, style)
    {
        words = 6;
        const unsigned a[] = {Win16ImageListNative(frame.Word(5)), Signed(frame.Word(4)), frame.Word(3),
                              Signed(frame.Word(2)), Signed(frame.Word(1)), frame.Word(0) & 1};
        result = a[0] ? ImageApi("ImageList_Draw", a) : 0;
        return true;
    }
    case 53: // ImageList_AddIcon(himl, hicon)
    case 54: // ImageList_ReplaceIcon(himl, i, hicon)
    {
        words = ordinal == 53 ? 2 : 3;
        const unsigned himl = frame.Word(words - 1), index = ordinal == 53 ? ~0u : Signed(frame.Word(1));
        const unsigned a[] = {Win16ImageListNative(himl), index, Win16ImageNative(frame.Word(0))};
        result = a[0] ? ImageApi("ImageList_ReplaceIcon", a) & 0xffff : 0xffff;
        return true;
    }
    default:
        break;
    }
    char text[160];
    snprintf(text, sizeof text, "Win16 COMMCTRL image list ordinal %u is not ported yet", ordinal);
    Fail(text);
    return false;
}

// A 16-bit TV_ITEM at `source` as a native TVITEMA at `target`.
void Application::Win16TreeItemIn(unsigned source, unsigned target)
{
    const unsigned mask = Win16Word(source);
    Write32(target + 0, mask);
    Write32(target + 4, Read32(source + 2));
    Write32(target + 8, Win16Word(source + 6));
    Write32(target + 12, Win16Word(source + 8));
    const unsigned text = Read32(source + 0x0a);
    Write32(target + 16, text == TextCallback || !(text >> 16) ? text : Win16Linear(text >> 16, text & 0xffff));
    Write32(target + 20, Signed(Win16Word(source + 0x0e)));
    Write32(target + 24, Signed(Win16Word(source + 0x10)));
    Write32(target + 28, Signed(Win16Word(source + 0x12)));
    Write32(target + 32, Signed(Win16Word(source + 0x14)));
    Write32(target + 36, Read32(source + 0x16));
}

// A native TVITEMA back into the 16-bit TV_ITEM; the text stays where the
// pointer of the 16-bit item named it.
void Application::Win16TreeItemOut(unsigned source, unsigned target, unsigned text16)
{
    Win16Word(target, Read32(source) & 0xffff);
    Write32(target + 2, Read32(source + 4));
    Win16Word(target + 6, Read32(source + 8) & 0xffff);
    Win16Word(target + 8, Read32(source + 12) & 0xffff);
    Write32(target + 0x0a, text16);
    Win16Word(target + 0x0e, Read32(source + 20) & 0xffff);
    Win16Word(target + 0x10, Read32(source + 24) & 0xffff);
    Win16Word(target + 0x12, Read32(source + 28) & 0xffff);
    Win16Word(target + 0x14, Read32(source + 32) & 0xffff);
    Write32(target + 0x16, Read32(source + 36));
}

// A message of 16-bit code to a native tree view (User::CommonControl 1).
// Answers true when the message was one of the tree's and has been sent.
bool Application::Win16TreeSend(unsigned hwnd, unsigned message, unsigned wparam, unsigned lparam,
                                unsigned &result)
{
    if (User::CommonControl(hwnd) != 1 || message < 0x1100 || message > 0x11ff)
        return false;
    const unsigned far_lparam = lparam;
    const unsigned flat = lparam >> 16 ? Win16Linear(lparam >> 16, lparam & 0xffff) : 0;
    auto send = [&](unsigned wp, unsigned lp) {
        const unsigned a[] = {hwnd, message, wp, lp};
        return Api("SendMessageA", a);
    };
    const unsigned block = Allocate(64);
    if (!block)
        return false;
    result = 0;
    switch (message)
    {
    case 0x1100: // TVM_INSERTITEM
        if (!flat)
            break;
        Write32(block, Read32(flat));
        Write32(block + 4, Read32(flat + 4));
        Win16TreeItemIn(flat + 8, block + 8);
        result = send(wparam, block);
        break;
    case 0x110c: // TVM_GETITEM
    case 0x110d: // TVM_SETITEM
        if (!flat)
            break;
        Win16TreeItemIn(flat, block);
        result = send(wparam, block);
        if (message == 0x110c)
            Win16TreeItemOut(block, flat, Read32(flat + 0x0a));
        break;
    case 0x1104: // TVM_GETITEMRECT: the item in, the rectangle out
        if (!flat)
            break;
        Write32(block, Read32(flat));
        result = send(wparam & 0xffff, block);
        for (unsigned i = 0; i < 4; ++i)
            Win16Word(flat + i * 2, Read32(block + i * 4) & 0xffff);
        break;
    case 0x1111: // TVM_HITTEST: TV_HITTESTINFO, point words, flags word, item dword
        if (!flat)
            break;
        Write32(block, Signed(Win16Word(flat)));
        Write32(block + 4, Signed(Win16Word(flat + 2)));
        result = send(0, block);
        Win16Word(flat + 4, Read32(block + 8) & 0xffff);
        Write32(flat + 6, Read32(block + 12));
        break;
    case 0x1108: // TVM_GETIMAGELIST
        result = Win16ImageListWord(send(wparam & 0xffff, 0));
        break;
    case 0x1109: // TVM_SETIMAGELIST
        result = Win16ImageListWord(send(wparam & 0xffff, Win16ImageListNative(lparam)));
        break;
    case 0x1101: // TVM_DELETEITEM
    case 0x1102: // TVM_EXPAND
    case 0x1105: // TVM_GETCOUNT
    case 0x1106: // TVM_GETINDENT
    case 0x1107: // TVM_SETINDENT
    case 0x110a: // TVM_GETNEXTITEM
    case 0x110b: // TVM_SELECTITEM
    case 0x110e: // TVM_EDITLABEL
    case 0x110f: // TVM_GETEDITCONTROL
    case 0x1110: // TVM_GETVISIBLECOUNT
    case 0x1113: // TVM_SORTCHILDREN
    case 0x1114: // TVM_ENSUREVISIBLE
    case 0x1116: // TVM_ENDEDITLABELNOW
        result = send(message == 0x1107 ? wparam & 0xffff : Signed(wparam), far_lparam);
        break;
    default:
    {
        Free(block);
        char text[160];
        snprintf(text, sizeof text, "Win16 COMMCTRL tree view message %04x is not ported yet", message);
        Fail(text);
        result = 0;
        return true;
    }
    }
    Free(block);
    return true;
}

// LV_ITEM of the 16-bit side, packed like TV_ITEM (COMMCTRL Windows 98 reads
// iSubItem at +4, state at +6, pszText at +0Ah): mask, iItem, iSubItem,
// state, stateMask words, pszText far pointer, cchTextMax and iImage words,
// lParam dword; 22 bytes. The native LVITEMA has 40.
void Application::Win16ListItemIn(unsigned source, unsigned target)
{
    unsigned char zero[40] = {};
    Store(target, zero, sizeof zero);
    Write32(target + 0, Win16Word(source));
    Write32(target + 4, Signed(Win16Word(source + 2)));
    Write32(target + 8, Signed(Win16Word(source + 4)));
    Write32(target + 12, Win16Word(source + 6));
    Write32(target + 16, Win16Word(source + 8));
    const unsigned text = Read32(source + 0x0a);
    Write32(target + 20, text == TextCallback || !(text >> 16) ? text : Win16Linear(text >> 16, text & 0xffff));
    Write32(target + 24, Signed(Win16Word(source + 0x0e)));
    Write32(target + 28, Signed(Win16Word(source + 0x10)));
    Write32(target + 32, Read32(source + 0x12));
}

void Application::Win16ListItemOut(unsigned source, unsigned target, unsigned text16)
{
    Win16Word(target, Read32(source) & 0xffff);
    Win16Word(target + 2, Read32(source + 4) & 0xffff);
    Win16Word(target + 4, Read32(source + 8) & 0xffff);
    Win16Word(target + 6, Read32(source + 12) & 0xffff);
    Win16Word(target + 8, Read32(source + 16) & 0xffff);
    Write32(target + 0x0a, text16);
    Win16Word(target + 0x0e, Read32(source + 24) & 0xffff);
    Win16Word(target + 0x10, Read32(source + 28) & 0xffff);
    Write32(target + 0x12, Read32(source + 32));
}

// A message of 16-bit code to a native list view (User::CommonControl 2),
// class "SysListView" of COMMCTRL (LISTVIEW_WNDPROC). Item indexes
// in wParam are words, -1 included. LV_COLUMN of the 16-bit side: mask,
// fmt, cx words, pszText far pointer, cchTextMax, iSubItem words (14 bytes).
bool Application::Win16ListSend(unsigned hwnd, unsigned message, unsigned wparam, unsigned lparam,
                                unsigned &result)
{
    if (User::CommonControl(hwnd) != 2 || message < 0x1000 || message > 0x10ff)
        return false;
    const unsigned flat = lparam >> 16 ? Win16Linear(lparam >> 16, lparam & 0xffff) : 0;
    auto send = [&](unsigned wp, unsigned lp) {
        const unsigned a[] = {hwnd, message, wp, lp};
        return Api("SendMessageA", a);
    };
    const unsigned block = Allocate(64);
    if (!block)
        return false;
    result = 0;
    switch (message)
    {
    case 0x1005: // LVM_GETITEM
    case 0x1006: // LVM_SETITEM
    case 0x1007: // LVM_INSERTITEM
    case 0x102b: // LVM_SETITEMSTATE
    case 0x102d: // LVM_GETITEMTEXT
    case 0x102e: // LVM_SETITEMTEXT
        if (!flat)
            break;
        Win16ListItemIn(flat, block);
        result = send(Signed(wparam), block);
        if (message == 0x1005)
            Win16ListItemOut(block, flat, Read32(flat + 0x0a));
        if (message == 0x1007 || message == 0x102b || message == 0x100c)
            result = Signed(result);
        break;
    case 0x1019: // LVM_GETCOLUMN
    case 0x101a: // LVM_SETCOLUMN
    case 0x101b: // LVM_INSERTCOLUMN
    {
        if (!flat)
            break;
        const unsigned text = Read32(flat + 6);
        Write32(block + 0, Win16Word(flat));
        Write32(block + 4, Signed(Win16Word(flat + 2)));
        Write32(block + 8, Signed(Win16Word(flat + 4)));
        Write32(block + 12, text == TextCallback || !(text >> 16) ? text : Win16Linear(text >> 16, text & 0xffff));
        Write32(block + 16, Signed(Win16Word(flat + 0x0a)));
        Write32(block + 20, Signed(Win16Word(flat + 0x0c)));
        result = send(Signed(wparam), block);
        if (message == 0x1019)
        {
            Win16Word(flat + 2, Read32(block + 4) & 0xffff);
            Win16Word(flat + 4, Read32(block + 8) & 0xffff);
            Win16Word(flat + 0x0c, Read32(block + 20) & 0xffff);
        }
        break;
    }
    case 0x100e: // LVM_GETITEMRECT: the code in rect.left, the rectangle out
        if (!flat)
            break;
        Write32(block, Signed(Win16Word(flat)));
        result = send(Signed(wparam), block);
        for (unsigned i = 0; i < 4; ++i)
            Win16Word(flat + i * 2, Read32(block + i * 4) & 0xffff);
        break;
    case 0x1012: // LVM_HITTEST: LV_HITTESTINFO, point words, flags and iItem words
        if (!flat)
            break;
        Write32(block, Signed(Win16Word(flat)));
        Write32(block + 4, Signed(Win16Word(flat + 2)));
        result = Signed(send(0, block));
        Win16Word(flat + 4, Read32(block + 8) & 0xffff);
        Win16Word(flat + 6, Read32(block + 12) & 0xffff);
        break;
    case 0x100d: // LVM_FINDITEM: LV_FINDINFO flags word, psz far pointer,
                 // lParam dword, pt words and vkDirection word (16 bytes)
        if (!flat)
            break;
        {
            const unsigned text = Read32(flat + 2);
            Write32(block + 0, Win16Word(flat));
            Write32(block + 4, text >> 16 ? Win16Linear(text >> 16, text & 0xffff) : text);
            Write32(block + 8, Read32(flat + 6));
            Write32(block + 12, Signed(Win16Word(flat + 10)));
            Write32(block + 16, Signed(Win16Word(flat + 12)));
            Write32(block + 20, Win16Word(flat + 14));
            result = Signed(send(Signed(wparam), block));
        }
        break;
    case 0x1002: // LVM_GETIMAGELIST
        result = Win16ImageListWord(send(wparam & 0xffff, 0));
        break;
    case 0x1003: // LVM_SETIMAGELIST
        result = Win16ImageListWord(send(wparam & 0xffff, Win16ImageListNative(lparam)));
        break;
    case 0x100c: // LVM_GETNEXTITEM(item, flags)
        result = Signed(send(Signed(wparam), lparam & 0xffff));
        break;
    case 0x1000: // LVM_GETBKCOLOR
    case 0x1001: // LVM_SETBKCOLOR
    case 0x1004: // LVM_GETITEMCOUNT
    case 0x1008: // LVM_DELETEITEM
    case 0x1009: // LVM_DELETEALLITEMS
    case 0x1013: // LVM_ENSUREVISIBLE
    case 0x101c: // LVM_DELETECOLUMN
    case 0x101d: // LVM_GETCOLUMNWIDTH
    case 0x1023: // LVM_GETTOPINDEX
    case 0x1024: // LVM_GETTEXTCOLOR
    case 0x1025: // LVM_SETTEXTCOLOR
    case 0x1026: // LVM_GETTEXTBKCOLOR
    case 0x1027: // LVM_SETTEXTBKCOLOR
    case 0x1028: // LVM_GETTOPINDEX
    case 0x102c: // LVM_GETITEMSTATE
    case 0x1032: // LVM_GETSELECTEDCOUNT
        result = send(Signed(wparam), lparam);
        break;
    case 0x101e: // LVM_SETCOLUMNWIDTH(column, MAKELPARAM(cx, 0))
        result = send(Signed(wparam), Signed(lparam & 0xffff));
        break;
    default:
    {
        Free(block);
        char text[160];
        snprintf(text, sizeof text, "Win16 COMMCTRL list view message %04x is not ported yet", message);
        Fail(text);
        result = 0;
        return true;
    }
    }
    Free(block);
    return true;
}

// The bytes of the 16-bit form of a WM_NOTIFY block, with room for the text
// of a TV_DISPINFO behind it.
unsigned Application::Win16NotifyBytes(unsigned lparam)
{
    const int code = int(Read32(lparam + 8));
    // NM_LISTVIEW (LVN_ITEMCHANGING to LVN_BEGINRDRAG), LV_DISPINFO, LV_KEYDOWN.
    if (code <= -100 && code >= -111 && code != -105 && code != -106)
        return 30;
    if (code == -150 || code == -151 || code == -105 || code == -106)
        return 12 + 22 + 260;
    if (code <= -401 && code >= -409 && code != -403 && code != -404)
        return 0x46;
    if (code == -403 || code == -404 || code == -410 || code == -411)
        return 12 + 26 + 260;
    if (code == -412)
        return 16;
    return 16;
}

// WM_NOTIFY on its way into 16-bit code: the header and, for the tree view,
// the structures of COMMCTRL. Everything else keeps the header and
// the dword behind it (PSHNOTIFY.lParam).
void Application::Win16NotifyTo16(unsigned lparam, unsigned at, unsigned far_at)
{
    const int code = int(Read32(lparam + 8));
    Write32(at + 0, Read32(lparam) & 0xffff);
    Write32(at + 4, Read32(lparam + 4) & 0xffff);
    Write32(at + 8, Read32(lparam + 8));
    const unsigned bytes = Win16NotifyBytes(lparam);
    if (bytes == 30)
    {
        // NM_LISTVIEW: iItem, iSubItem, uNewState, uOldState, uChanged,
        // ptAction words and lParam, packed behind the header.
        for (unsigned i = 0; i < 5; ++i)
            Win16Word(at + 12 + i * 2, Read32(lparam + 12 + i * 4) & 0xffff);
        Win16Word(at + 22, Read32(lparam + 32) & 0xffff);
        Win16Word(at + 24, Read32(lparam + 36) & 0xffff);
        Write32(at + 26, Read32(lparam + 40));
    }
    else if (bytes == 12 + 22 + 260)
    {
        const unsigned text = Read32(lparam + 12 + 20), capacity = Read32(lparam + 12 + 24);
        Win16ListItemOut(lparam + 12, at + 12, far_at + 34);
        Win16Word(at + 12 + 0x0e, capacity > 260 ? 260 : capacity);
        unsigned char copy[260] = {};
        if (text && text != TextCallback && (Read32(lparam + 12) & 1))
            Read(text, copy, capacity && capacity < 259 ? capacity : 259);
        copy[259] = 0;
        Store(at + 34, copy, 260);
    }
    else if (code == -155)
    {
        Win16Word(at + 12, Read32(lparam + 12) & 0xffff);
        Win16Word(at + 14, Read32(lparam + 14) & 0xffff);
    }
    else if (bytes == 0x46)
    {
        Win16Word(at + 0x0c, Read32(lparam + 12) & 0xffff);
        Win16TreeItemOut(lparam + 16, at + 0x0e, 0);
        Win16TreeItemOut(lparam + 56, at + 0x28, 0);
        Win16Word(at + 0x42, Read32(lparam + 96) & 0xffff);
        Win16Word(at + 0x44, Read32(lparam + 100) & 0xffff);
    }
    else if (bytes == 12 + 26 + 260)
    {
        // TV_DISPINFO: the text goes to the block behind the item.
        const unsigned text = Read32(lparam + 12 + 16), capacity = Read32(lparam + 12 + 20);
        const unsigned buffer16 = far_at + 38;
        Win16TreeItemOut(lparam + 12, at + 12, buffer16);
        Win16Word(at + 12 + 0x0e, capacity > 260 ? 260 : capacity);
        unsigned char copy[260] = {};
        if (text && text != TextCallback && (Read32(lparam + 12) & TvifText))
            Read(text, copy, capacity && capacity < 259 ? capacity : 259);
        copy[259] = 0;
        Store(at + 38, copy, 260);
    }
    else if (code == -412)
    {
        Win16Word(at + 12, Read32(lparam + 12) & 0xffff);
        Win16Word(at + 14, Read32(lparam + 14) & 0xffff);
    }
    else
        Write32(at + 12, Read32(lparam + 12));
}

// What a 16-bit answer changed: the item of TVN_GETDISPINFO with its text.
void Application::Win16NotifyBack(unsigned lparam, unsigned at, unsigned far_at)
{
    const int code = int(Read32(lparam + 8));
    if (code == -150 || code == -106)
    {
        // LVN_GETDISPINFO and LVN_ENDLABELEDIT: the item the page filled in.
        const unsigned item = lparam + 12, text = Read32(item + 20), capacity = Read32(item + 24);
        const unsigned mask = Win16Word(at + 12);
        Write32(item + 12, Win16Word(at + 12 + 6));
        Write32(item + 28, Signed(Win16Word(at + 12 + 0x10)));
        const unsigned text16 = Read32(at + 12 + 0x0a);
        if (!(mask & 1) || !text16 || text16 == TextCallback)
            return;
        const unsigned source = text16 == far_at + 34 ? at + 34 : Win16Linear(text16 >> 16, text16 & 0xffff);
        if (code == -106)
        {
            Write32(item + 20, source);
            return;
        }
        const char *string = text && capacity ? ReadString(source) : nullptr;
        if (!string)
            return;
        unsigned length = unsigned(strlen(string));
        if (length >= capacity)
            length = capacity - 1;
        Store(text, string, length);
        const unsigned char zero = 0;
        Store(text + length, &zero, 1);
        return;
    }
    if (code != -403 && code != -410 && code != -411)
        return;
    const unsigned item = lparam + 12, text = Read32(item + 16), capacity = Read32(item + 20);
    const unsigned mask = Win16Word(at + 12);
    Write32(item, mask);
    Write32(item + 8, Win16Word(at + 12 + 6));
    Write32(item + 24, Signed(Win16Word(at + 12 + 0x10)));
    Write32(item + 28, Signed(Win16Word(at + 12 + 0x12)));
    Write32(item + 32, Signed(Win16Word(at + 12 + 0x14)));
    Write32(item + 36, Read32(at + 12 + 0x16));
    const unsigned text16 = Read32(at + 12 + 0x0a);
    if (!(mask & TvifText) || !text16 || text16 == TextCallback)
        return;
    const unsigned source = text16 == far_at + 38 ? at + 38 : Win16Linear(text16 >> 16, text16 & 0xffff);
    if (code == -411)
    {
        Write32(item + 16, source);
        return;
    }
    if (!text || !capacity)
        return;
    const char *string = ReadString(source);
    if (!string)
        return;
    unsigned length = unsigned(strlen(string));
    if (length >= capacity)
        length = capacity - 1;
    Store(text, string, length);
    const unsigned char zero = 0;
    Store(text + length, &zero, 1);
}
} // namespace X86
