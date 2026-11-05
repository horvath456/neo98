// SPDX-License-Identifier: GPL-2.0-or-later
// Win16 USER gates either marshal arguments to native Win32 services,
// enumerate windows through a Win16 callback, or return a system constant.
// WNet calls return WN_NOT_SUPPORTED because no network provider is
// installed. Argument WORD counts are defined in src/win98_win16_user.h.
bool Application::Win16UserMore(unsigned ordinal, const Win16Frame &frame, unsigned words, unsigned &result,
                                bool &handled)
{
    handled = true;
    auto arg = [&](unsigned position) { return frame.Word(words - 1 - position); };
    auto pointer = [&](unsigned position) { return frame.Pointer(words - 2 - position); };
    auto dword = [&](unsigned position) { return frame.Long(words - 2 - position); };
    auto signed_word = [&](unsigned position) { return (unsigned)(int)(short)arg(position); };
    // The 16-bit RECT at `from` widened at `to`; null stays null.
    auto rect_in = [&](unsigned from, unsigned to) -> unsigned {
        if (!from)
            return 0;
        Gdi::RECT r{};
        Win16ReadRect(from, r);
        Store(to, &r, sizeof r);
        return to;
    };
    auto rect_out = [&](unsigned from, unsigned to) {
        if (!to)
            return;
        const Gdi::RECT r{(int)Read32(from), (int)Read32(from + 4), (int)Read32(from + 8), (int)Read32(from + 12)};
        Win16WriteRect(to, r);
    };
    auto point_out = [&](unsigned from, unsigned to) {
        if (!to)
            return;
        Win16Word(to, Read32(from));
        Win16Word(to + 2, Read32(from + 4));
    };
    // A POINT passed by value is one long whose low word is x, the memory
    // image of the structure (USER reads x at [sp+4]).
    auto point_x = [&](unsigned position) { return (unsigned)(int)(short)(dword(position) & 0xffff); };
    auto point_y = [&](unsigned position) { return (unsigned)(int)(short)(dword(position) >> 16); };
    // An icon or cursor the native side made, under its word alias; the
    // native one goes again when no alias is left.
    auto alias = [&](unsigned native, unsigned instance, bool cursor) -> unsigned {
        if (!native)
            return 0;
        const unsigned word = Win16ImageAlias(native, instance, cursor, 0, nullptr);
        if (!word)
        {
            const unsigned destroy[] = {native};
            Api("DestroyIcon", destroy);
        }
        return word;
    };
    // A far string argument or the plain value an item of a menu carries
    // instead (InsertMenu decides the same way, USER.410).
    auto text_or_value = [&](unsigned position) { return arg(position) ? pointer(position) : arg(position + 1); };
    // The handle of a deferred positioning, narrowed to a word: the batch
    // index and the low byte of its generation. PositionApi checks the whole
    // generation, so a stale word still fails there.
    auto defer_word = [&](unsigned native) -> unsigned {
        return native ? ((native & 0xff) | (((native >> 8) & 0xff) << 8)) : 0u;
    };
    auto defer_native = [&](unsigned word) -> unsigned {
        const unsigned i = (word & 0xff) - 1;
        if (!word || i >= 8 || !positions[i].used || (positions[i].generation & 0xff) != (word >> 8))
            return 0;
        return 0x20000000u | (positions[i].generation << 8) | (i + 1);
    };
    switch (ordinal)
    {
    // ---- constants of USER.EXE itself
    case 14: // GetTimerResolution: `xor dx, dxmov ax, 1000`.
        result = 1000;
        return true;
    case 480: // GetUserLocalObjType(handle): `xor ax, axretf 2`.
        result = 0;
        return true;
    case 189: // GetSysModalWindow: USER reads the window SetSysModalWindow
              // kept, and USER.188 keeps none here.
        result = 0;
        return true;
    case 501: case 502: case 503: case 504: case 505: case 506: case 507: case 508: case 509:
    case 510: case 511: case 514: case 515: case 516: case 517: case 518: case 519: case 520:
    case 521: case 522: case 523: case 524: case 525: case 526: case 527: case 528: case 529:
    case 530: case 531: case 532: case 534:
        result = 1; // WN_NOT_SUPPORTED
        return true;

    // ---- plain words
    case 20: { const unsigned a[] = {arg(0)}; Api("SetDoubleClickTime", a); result = 0; return true; }
    case 21: { const unsigned a[] = {}; result = Api("GetDoubleClickTime", a); return true; }
    case 44: { const unsigned a[] = {arg(0)}; result = Api("OpenIcon", a); return true; }
    case 52: { const unsigned a[] = {}; result = Api("AnyPopup", a); return true; }
    case 115: { const unsigned a[] = {dword(0)}; Api("ReplyMessage", a); result = 0; return true; }
    case 128: { const unsigned a[] = {arg(0), arg(1)}; Api("ValidateRgn", a); result = 0; return true; }
    case 162: { const unsigned a[] = {arg(0), arg(1), arg(2), arg(3)}; result = Api("HiliteMenuItem", a); return true; }
    case 168: { const unsigned a[] = {arg(0)}; Api("SetCaretBlinkTime", a); result = 0; return true; }
    case 169: { const unsigned a[] = {}; result = Api("GetCaretBlinkTime", a); return true; }
    case 170: { const unsigned a[] = {arg(0)}; result = Api("ArrangeIconicWindows", a); return true; }
    case 172: { const unsigned a[] = {arg(0), arg(1)}; Api("SwitchToThisWindow", a); result = 0; return true; }
    case 186: { const unsigned a[] = {arg(0)}; result = Api("SwapMouseButton", a); return true; }
    case 187: { const unsigned a[] = {}; Api("EndMenu", a); result = 0; return true; }
    case 192: { const unsigned a[] = {}; result = Api("InSendMessage", a); return true; }
    case 193: { const unsigned a[] = {arg(0)}; result = Api("IsClipboardFormatAvailable", a); return true; }
    case 198: { const unsigned a[] = {arg(0), arg(1)}; Api("CascadeChildWindows", a); result = 0; return true; }
    case 199: { const unsigned a[] = {arg(0), arg(1)}; Api("TileChildWindows", a); result = 0; return true; }
    case 227: { const unsigned a[] = {arg(0), arg(1), arg(2)}; result = Api("GetNextDlgGroupItem", a); return true; }
    case 233: { const unsigned a[] = {arg(0), arg(1)}; result = Api("SetParent", a); return true; }
    case 238: { const unsigned a[] = {arg(0), arg(1)}; result = Api("ExcludeUpdateRgn", a); return true; }
    case 248: { const unsigned a[] = {}; result = Api("GetOpenClipboardWindow", a); return true; }
    case 249: { const unsigned a[] = {arg(0)}; result = Api("GetAsyncKeyState", a) & 0xffff; return true; }
    case 265: { const unsigned a[] = {arg(0), arg(1)}; Api("ShowOwnedPopups", a); result = 0; return true; }
    case 267: { const unsigned a[] = {arg(0), arg(1), arg(2)}; Api("ShowScrollBar", a); result = 0; return true; }
    case 288: { const unsigned a[] = {}; result = Api("GetMessageExtraInfo", a); return true; }
    case 376: { const unsigned a[] = {dword(0)}; result = Api("SetMessageExtraInfo", a); return true; }
    case 294: { const unsigned a[] = {arg(0)}; result = Api("LockWindowUpdate", a); return true; }
    case 334: { const unsigned a[] = {arg(0)}; result = Api("GetQueueStatus", a); return true; }
    case 335: { const unsigned a[] = {}; result = Api("GetInputState", a); return true; }
    case 358: { const unsigned a[] = {arg(0)}; result = Api("IsMenu", a); return true; }
    case 359: { const unsigned a[] = {arg(0), arg(1), dword(2)}; result = Api("GetDCEx", a); return true; }
    case 382: { const unsigned a[] = {arg(0), dword(1)}; result = Api("SetWindowContextHelpId", a); return true; }
    case 383: { const unsigned a[] = {arg(0)}; result = Api("GetWindowContextHelpId", a); return true; }
    case 384: { const unsigned a[] = {arg(0), dword(1)}; result = Api("SetMenuContextHelpId", a); return true; }
    case 385: { const unsigned a[] = {arg(0)}; result = Api("GetMenuContextHelpId", a); return true; }
    case 412: { const unsigned a[] = {arg(0), arg(1), arg(2)}; result = Api("RemoveMenu", a); return true; }
    case 415: { const unsigned a[] = {}; result = Api("CreatePopupMenu", a); return true; }
    case 417: { const unsigned a[] = {}; result = Api("GetMenuCheckMarkDimensions", a); return true; }
    case 418: { const unsigned a[] = {arg(0), arg(1), arg(2), arg(3), arg(4)}; result = Api("SetMenuItemBitmaps", a); return true; }
    case 435: { const unsigned a[] = {arg(0) & 0xff}; result = Api("IsCharUpperA", a); return true; }
    case 436: { const unsigned a[] = {arg(0) & 0xff}; result = Api("IsCharLowerA", a); return true; }
    case 464: { const unsigned a[] = {arg(0), arg(1), arg(2), dword(3), Win16ImageNative(arg(5))}; result = Api("DragObject", a); return true; }
    case 600: { const unsigned a[] = {}; result = Api("GetShellWindow", a); return true; }
    case 608: { const unsigned a[] = {}; result = Api("GetForegroundWindow", a); return true; }
    case 609: { const unsigned a[] = {arg(0)}; result = Api("SetForegroundWindow", a); return true; }
    case 663: { const unsigned a[] = {arg(0), arg(1), arg(2)}; result = Api("GetMenuDefaultItem", a) & 0xffff; return true; }
    case 664: { const unsigned a[] = {arg(0), signed_word(1), arg(2)}; result = Api("SetMenuDefaultItem", a); return true; }
    case 666: { const unsigned a[] = {arg(0), arg(1), arg(2), arg(3), arg(4)}; result = Api("CheckMenuRadioItem", a); return true; }
    case 668: { const unsigned a[] = {arg(0), arg(1), arg(2)}; result = Api("SetWindowRgn", a); return true; }
    case 669: { const unsigned a[] = {arg(0), arg(1)}; result = Api("GetWindowRgn", a); return true; }
    // The keyboard layouts: an HKL is a DWORD on this side too.
    case 477: { const unsigned a[] = {pointer(0)}; result = Api("GetKeyboardLayoutNameA", a); return true; }
    case 478: { const unsigned a[] = {pointer(0), arg(2)}; result = Api("LoadKeyboardLayoutA", a); return true; }
    case 650: { const unsigned a[] = {dword(0), arg(2)}; result = Api("ActivateKeyboardLayout", a); return true; }
    case 651: { const unsigned a[] = {dword(0)}; result = Api("GetKeyboardLayout", a); return true; }
    case 652: { const unsigned a[] = {signed_word(0), pointer(1)}; result = Api("GetKeyboardLayoutList", a) & 0xffff; return true; }
    case 654: { const unsigned a[] = {dword(0)}; result = Api("UnloadKeyboardLayout", a); return true; }
    case 222: { const unsigned a[] = {pointer(0)}; Api("GetKeyboardState", a); result = 0; return true; }
    case 223: { const unsigned a[] = {pointer(0)}; Api("SetKeyboardState", a); result = 0; return true; }
    // DEVMODE has the same fields and widths on both sides.
    case 620: { const unsigned a[] = {pointer(0), dword(2)}; result = Api("ChangeDisplaySettingsA", a); return true; }
    case 621: { const unsigned a[] = {pointer(0), dword(2), pointer(4)}; result = Api("EnumDisplaySettingsA", a); return true; }
    // The handles of the array are kernel handles, dwords on both sides.
    case 640:
    {
        const unsigned a[] = {dword(0), pointer(2), arg(4), dword(5), dword(7)};
        result = Api("MsgWaitForMultipleObjects", a);
        return true;
    }

    case 123: // CallMsgFilter(message, code): the MSG widened as GetMessage
              // writes it, for the message filter hooks of USER32.
    {
        const unsigned at = pointer(0), scratch = Win16Scratch();
        if (!scratch) return false;
        if (!at) { result = 0; return true; }
        User::Message message{};
        Win16ReadMessage(at, message);
        Store(scratch, &message, sizeof message);
        const unsigned a[] = {scratch, signed_word(2)};
        result = Api("CallMsgFilterA", a);
        return true;
    }

    // ---- points
    case 183: // GetCaretPos(point)
    {
        const unsigned scratch = Win16Scratch();
        if (!scratch) return false;
        const unsigned a[] = {scratch};
        Api("GetCaretPos", a);
        point_out(scratch, pointer(0));
        result = 0;
        return true;
    }
    case 191: { const unsigned a[] = {arg(0), point_x(1), point_y(1)}; result = Api("ChildWindowFromPoint", a); return true; }
    case 399: { const unsigned a[] = {arg(0), point_x(1), point_y(1), arg(3)}; result = Api("ChildWindowFromPointEx", a); return true; }
    case 465: { const unsigned a[] = {arg(0), point_x(1), point_y(1)}; result = Api("DragDetect", a); return true; }
    case 479: { const unsigned a[] = {arg(0), arg(1), point_x(2), point_y(2)}; result = Api("MenuItemFromPoint", a) & 0xffff; return true; }

    // ---- rectangles
    case 82: // InvertRect(hdc, rect)
    {
        const unsigned scratch = Win16Scratch();
        if (!scratch) return false;
        const unsigned a[] = {arg(0), rect_in(pointer(1), scratch)};
        Api("InvertRect", a);
        result = 0;
        return true;
    }
    case 103: // MapDialogRect(dialog, rect): the rectangle in and out.
    case 309: // GetClipCursor(rect)
    case 665: // GetMenuItemRect(hwnd, menu, item, rect)
    {
        const unsigned scratch = Win16Scratch();
        if (!scratch) return false;
        if (ordinal == 103)
        {
            const unsigned a[] = {arg(0), rect_in(pointer(1), scratch)};
            if (a[1]) Api("MapDialogRect", a);
            rect_out(scratch, pointer(1));
            result = 0;
            return true;
        }
        if (ordinal == 309)
        {
            const unsigned a[] = {scratch};
            Api("GetClipCursor", a);
            rect_out(scratch, pointer(0));
            result = 0;
            return true;
        }
        const unsigned a[] = {arg(0), arg(1), arg(2), scratch};
        result = Api("GetMenuItemRect", a);
        if (result)
            rect_out(scratch, pointer(3));
        return true;
    }
    case 373: // SubtractRect(out, a, b)
    {
        const unsigned scratch = Win16Scratch();
        if (!scratch) return false;
        const unsigned a[] = {scratch, rect_in(pointer(2), scratch + 16), rect_in(pointer(4), scratch + 32)};
        result = a[1] && a[2] ? Api("SubtractRect", a) : 0;
        if (a[1] && a[2])
            rect_out(scratch, pointer(0));
        return true;
    }
    case 221: // ScrollDC(hdc, dx, dy, scroll, clip, update region, update rect)
    case 319: // ScrollWindowEx(hwnd, dx, dy, scroll, clip, update region, update rect, flags)
    {
        const unsigned scratch = Win16Scratch();
        if (!scratch) return false;
        const unsigned scroll = rect_in(pointer(3), scratch), clip = rect_in(pointer(5), scratch + 16);
        const unsigned update = pointer(8) ? scratch + 32 : 0u;
        if (ordinal == 221)
        {
            const unsigned a[] = {arg(0), signed_word(1), signed_word(2), scroll, clip, arg(7), update};
            result = Api("ScrollDC", a);
        }
        else
        {
            const unsigned a[] = {arg(0), signed_word(1), signed_word(2), scroll, clip, arg(7), update, arg(10)};
            result = Api("ScrollWindowEx", a);
        }
        if (update)
            rect_out(update, pointer(8));
        return true;
    }
    case 416: // TrackPopupMenu(menu, flags, x, y, reserved, hwnd, rect)
    {
        const unsigned scratch = Win16Scratch();
        if (!scratch) return false;
        const unsigned a[] = {arg(0), arg(1), signed_word(2), signed_word(3), signed_word(4), arg(5),
                              rect_in(pointer(6), scratch)};
        result = Api("TrackPopupMenu", a);
        return true;
    }
    case 667: // TrackPopupMenuEx(menu, flags, x, y, hwnd, TPMPARAMS): cbSize
              // and a RECT, eleven bytes wide on this side and twenty there.
    {
        const unsigned scratch = Win16Scratch(), params = pointer(5);
        if (!scratch) return false;
        if (params)
        {
            Write32(scratch, 20);
            rect_in(params + 2, scratch + 4);
        }
        const unsigned a[] = {arg(0), arg(1), signed_word(2), signed_word(3), arg(4), params ? scratch : 0u};
        result = Api("TrackPopupMenuEx", a);
        return true;
    }
    case 656: // DrawFrameControl(hdc, rect, type, state)
    case 659: // DrawEdge(hdc, rect, edge, flags): BF_ADJUST writes the rect.
    {
        const unsigned scratch = Win16Scratch();
        if (!scratch) return false;
        const unsigned a[] = {arg(0), rect_in(pointer(1), scratch), arg(3), arg(4)};
        result = Api(ordinal == 656 ? "DrawFrameControl" : "DrawEdge", a);
        if (a[1])
            rect_out(scratch, pointer(1));
        return true;
    }
    case 660: // DrawCaption(hwnd, hdc, rect, flags)
    {
        const unsigned scratch = Win16Scratch();
        if (!scratch) return false;
        const unsigned a[] = {arg(0), arg(1), rect_in(pointer(2), scratch), arg(4)};
        result = Api("DrawCaption", a);
        return true;
    }
    case 657: // DrawCaptionTemp(hwnd, hdc, rect, font, icon, text, flags)
    {
        const unsigned scratch = Win16Scratch();
        if (!scratch) return false;
        const unsigned a[] = {arg(0), arg(1), rect_in(pointer(2), scratch), arg(4), Win16ImageNative(arg(5)),
                              pointer(6), arg(8)};
        result = Api("DrawCaptionTempA", a);
        return true;
    }
    case 428: // TileWindows(parent, how, rect, count, kids)
    case 429: // CascadeWindows(parent, how, rect, count, kids): the windows
              // are words, the native array dwords.
    {
        const unsigned scratch = Win16Scratch(), kids = pointer(5);
        if (!scratch) return false;
        unsigned count = arg(4);
        if (count > (4096 - 16) / 4) count = (4096 - 16) / 4;
        for (unsigned i = 0; kids && i < count; ++i)
            Write32(scratch + 16 + i * 4, Win16Word(kids + i * 2));
        const unsigned a[] = {arg(0), arg(1), rect_in(pointer(2), scratch), kids ? count : 0u,
                              kids ? scratch + 16 : 0u};
        result = Api(ordinal == 428 ? "TileWindows" : "CascadeWindows", a) & 0xffff;
        return true;
    }

    // ---- arrays of words widened to dwords
    case 181: // SetSysColors(count, elements, colours)
    {
        const unsigned scratch = Win16Scratch(), elements = pointer(1);
        if (!scratch) return false;
        unsigned count = arg(0);
        if (count > 4096 / 4) count = 4096 / 4;
        for (unsigned i = 0; elements && i < count; ++i)
            Write32(scratch + i * 4, (unsigned)(int)(short)Win16Word(elements + i * 2));
        const unsigned a[] = {count, elements ? scratch : 0u, pointer(3)};
        Api("SetSysColors", a);
        result = 0;
        return true;
    }
    case 402: // GetPriorityClipboardFormat(list, count)
    {
        const unsigned scratch = Win16Scratch(), list = pointer(0);
        if (!scratch) return false;
        int count = (short)arg(2);
        if (count > 4096 / 4) count = 4096 / 4;
        for (int i = 0; list && i < count; ++i)
            Write32(scratch + i * 4, Win16Word(list + i * 2));
        const unsigned a[] = {list ? scratch : 0u, (unsigned)count};
        result = Api("GetPriorityClipboardFormat", a) & 0xffff;
        return true;
    }
    case 196: // TabbedTextOut(hdc, x, y, text, count, tabs, positions, origin)
    case 197: // GetTabbedTextExtent(hdc, text, count, tabs, positions)
    {
        const unsigned scratch = Win16Scratch();
        if (!scratch) return false;
        const unsigned first = ordinal == 196 ? 3 : 1;
        const unsigned tabs = pointer(first + 4);
        int count = (short)arg(first + 3);
        if (count < 0) count = 0;
        if (count > 4096 / 4) count = 4096 / 4;
        for (int i = 0; tabs && i < count; ++i)
            Write32(scratch + i * 4, (unsigned)(int)(short)Win16Word(tabs + i * 2));
        if (ordinal == 196)
        {
            const unsigned a[] = {arg(0), signed_word(1), signed_word(2), pointer(3), signed_word(5),
                                  (unsigned)count, tabs ? scratch : 0u, signed_word(9)};
            result = Api("TabbedTextOutA", a);
        }
        else
        {
            const unsigned a[] = {arg(0), pointer(1), signed_word(3), (unsigned)count, tabs ? scratch : 0u};
            result = Api("GetTabbedTextExtentA", a);
        }
        return true;
    }

    // ---- structures
    case 375: // DrawTextEx(hdc, text, count, rect, format, params): the
              // format is a DWORD here (USER reads ten words), and
              // DRAWTEXTPARAMS is five words.
    {
        const unsigned scratch = Win16Scratch(), params = pointer(8);
        if (!scratch) return false;
        const unsigned rect = rect_in(pointer(4), scratch);
        if (params)
        {
            Write32(scratch + 16, 20);
            for (unsigned i = 1; i < 5; ++i)
                Write32(scratch + 16 + i * 4, i < 4 ? (unsigned)(int)(short)Win16Word(params + i * 2)
                                                    : Win16Word(params + i * 2));
        }
        const unsigned a[] = {arg(0), pointer(1), signed_word(3), rect, dword(6), params ? scratch + 16 : 0u};
        result = Api("DrawTextExA", a);
        if (rect)
            rect_out(scratch, pointer(4));
        if (params)
            Win16Word(params + 8, Read32(scratch + 32));
        return true;
    }
    case 475: // SetScrollInfo(hwnd, bar, info, redraw)
    case 476: // GetScrollInfo(hwnd, bar, info): SCROLLINFO is seven words.
    {
        const unsigned scratch = Win16Scratch(), info = pointer(2);
        if (!scratch) return false;
        if (!info) { result = 0; return true; }
        Write32(scratch, 28);
        for (unsigned i = 1; i < 7; ++i)
        {
            const unsigned word = Win16Word(info + i * 2);
            Write32(scratch + i * 4, i == 1 || i == 4 ? word : (unsigned)(int)(short)word);
        }
        if (ordinal == 475)
        {
            const unsigned a[] = {arg(0), signed_word(1), scratch, arg(4)};
            result = Api("SetScrollInfo", a);
            return true;
        }
        const unsigned a[] = {arg(0), signed_word(1), scratch};
        result = Api("GetScrollInfo", a);
        const unsigned mask = Win16Word(info + 2);
        for (unsigned i = 2; result && i < 7; ++i)
            if ((i <= 3 && (mask & 1)) || (i == 4 && (mask & 2)) || (i == 5 && (mask & 4)) || (i == 6 && (mask & 16)))
                Win16Word(info + i * 2, Read32(scratch + i * 4));
        return true;
    }
    case 441: // InsertMenuItem(menu, item, by position, info)
    case 443: // GetMenuItemInfo(menu, item, by position, info)
    case 446: // SetMenuItemInfo(menu, item, by position, info)
    {
        // MENUITEMINFO of the 16-bit side: cbSize, fMask, fType, fState, wID,
        // hSubMenu, hbmpChecked, hbmpUnchecked as words, dwItemData,
        // dwTypeData as a far pointer or a value, and cch; 26 bytes against
        // the 44 of MENUITEMINFOA.
        const unsigned scratch = Win16Scratch(), info = pointer(3);
        if (!scratch) return false;
        if (!info) { result = 0; return true; }
        // A string item (MIIM_TYPE without MFT_BITMAP, MFT_OWNERDRAW or
        // MFT_SEPARATOR) carries a far pointer. GetMenuItemInfo learns the
        // type only from the answer, so there a buffer with a selector is the
        // pointer and the value of another type is written back afterwards.
        const unsigned mask = Win16Word(info + 2), type = Win16Word(info + 4);
        const unsigned type_data = Win16Word(info + 20) | Win16Word(info + 22) << 16;
        constexpr unsigned NotText = 0x4 | 0x100 | 0x800;
        const bool string = (mask & 0x10) && (ordinal == 443 ? (type_data >> 16) != 0 : !(type & NotText));
        Write32(scratch, 44);
        for (unsigned i = 1; i < 8; ++i)
            Write32(scratch + i * 4, Win16Word(info + i * 2));
        Write32(scratch + 32, Win16Word(info + 16) | Win16Word(info + 18) << 16);
        Write32(scratch + 36, string ? (type_data >> 16 ? Win16Linear(type_data >> 16, type_data & 0xffff) : 0u)
                                     : type_data & 0xffff);
        Write32(scratch + 40, Win16Word(info + 24));
        const unsigned a[] = {arg(0), arg(1), arg(2), scratch};
        result = Api(ordinal == 441 ? "InsertMenuItemA" : ordinal == 443 ? "GetMenuItemInfoA" : "SetMenuItemInfoA", a);
        if (ordinal == 443 && result)
        {
            for (unsigned i = 2; i < 8; ++i)
                Win16Word(info + i * 2, Read32(scratch + i * 4));
            const unsigned data = Read32(scratch + 32);
            Win16Word(info + 16, data & 0xffff);
            Win16Word(info + 18, data >> 16);
            if ((mask & 0x10) && (!string || (Read32(scratch + 8) & NotText)))
            {
                Win16Word(info + 20, Read32(scratch + 36));
                Win16Word(info + 22, 0);
            }
            Win16Word(info + 24, Read32(scratch + 40));
        }
        return true;
    }
    case 153: // ChangeMenu(menu, command, new item, insert, flags): the
              // Windows 2 form, which USER32 still carries.
    {
        const unsigned a[] = {arg(0), arg(1), text_or_value(2), arg(4), arg(5)};
        result = Api("ChangeMenuA", a);
        return true;
    }
    case 220: // LoadMenuIndirect(template): the same widening as LoadMenu, over
              // the bytes up to the end of the caller's segment.
    {
        const unsigned selector = frame.Word(1), offset = frame.Word(0), at = pointer(0);
        const unsigned limit = selector ? Win16Limit(selector) : 0;
        if (!at || offset > limit) { result = 0; return true; }
        unsigned bytes = limit - offset + 1;
        if (bytes > 65536) bytes = 65536;
        auto *copy = new unsigned char[bytes];
        if (!Read(at, copy, bytes))
        {
            delete[] copy;
            result = 0;
            return true;
        }
        result = Win16MenuFromTemplate(copy, bytes);
        delete[] copy;
        return state != Failed;
    }
    case 397: // RegisterClassEx(class): a DWORD cbSize, then the fields of
              // WNDCLASS, then hIconSm.
    {
        const unsigned at = pointer(0);
        if (!at) { result = 0; return true; }
        result = Win16RegisterClassAt(at + 4, Win16ImageNative(Win16Word(at + 4 + Win16U::ClassBytes)));
        return true;
    }
    case 398: // GetClassInfoEx(instance, class, out)
    {
        const unsigned out = pointer(3);
        const unsigned klass = arg(1) ? pointer(1) : arg(2);
        result = Win16ClassInfoInto(arg(0), klass, out ? out + 4 : 0u);
        // The small icon stays null: the 16-bit class store keeps none.
        if (result && out)
            Win16Word(out + 4 + Win16U::ClassBytes, 0);
        return true;
    }

    // ---- icons, cursors and images
    case 247: // GetCursor, under the word SetCursor would hand back.
    {
        const unsigned a[] = {};
        const unsigned native = Api("GetCursor", a);
        result = Win16ImageWord(native);
        if (native && !result)
            result = Win16ImageAlias(native, 0, true, 0, nullptr);
        return true;
    }
    case 458: // DestroyCursor(cursor)
        result = Win16DestroyImage(arg(0));
        return true;
    case 394: // DrawIconEx(hdc, x, y, icon, cx, cy, step, brush, flags)
    {
        const unsigned a[] = {arg(0), signed_word(1), signed_word(2), Win16ImageNative(arg(3)), signed_word(4),
                              signed_word(5), arg(6), arg(7), arg(8)};
        result = Api("DrawIconEx", a);
        return true;
    }
    case 406: // CreateCursor(instance, x, y, cx, cy, AND bits, XOR bits)
    {
        const unsigned a[] = {arg(0), signed_word(1), signed_word(2), signed_word(3), signed_word(4), pointer(5),
                              pointer(7)};
        result = alias(Api("CreateCursor", a), arg(0), true);
        return true;
    }
    case 364: // LookupIconIdFromDirectoryEx(directory, icon, cx, cy, flags)
    {
        const unsigned a[] = {pointer(0), arg(2), signed_word(3), signed_word(4), arg(5)};
        result = Api("LookupIconIdFromDirectoryEx", a);
        return true;
    }
    case 450: // CreateIconFromResourceEx(bits, size, icon, version, cx, cy, flags)
    {
        const unsigned a[] = {pointer(0), dword(2), arg(4), dword(5), signed_word(7), signed_word(8), arg(9)};
        result = alias(Api("CreateIconFromResourceEx", a), 0, !arg(4));
        return true;
    }
    case 390: // CopyImage(instance, image, type, cx, cy, flags): USER
              // reads six words, the instance first.
    {
        const unsigned type = arg(2);
        const unsigned image = type == 0 ? arg(1) : Win16ImageNative(arg(1));
        const unsigned a[] = {image, type, signed_word(3), signed_word(4), arg(5)};
        const unsigned copy = image ? Api("CopyImage", a) : 0u;
        result = type == 0 ? copy : alias(copy, arg(0), type == 2);
        return true;
    }
    case 449: // DrawState(hdc, brush, procedure, data, length, x, y, cx, cy, flags)
    {
        // Without a procedure the data is the string of DST_TEXT and
        // DST_PREFIXTEXT, the icon of DST_ICON or the bitmap of DST_BITMAP.
        // DST_COMPLEX draws through the program's own procedure, which the
        // native entry would call with the frame of a hook.
        const unsigned flags = arg(11), kind = flags & 15, procedure = dword(2);
        if (procedure || kind == 0)
        {
            Fail("Win16 gate USER.449 DrawState with an output procedure (DST_COMPLEX) is not carried");
            return false;
        }
        const unsigned data = kind == 1 || kind == 2 ? pointer(4) : kind == 3 ? Win16ImageNative(dword(4) & 0xffff)
                                                                            : dword(4) & 0xffff;
        const unsigned a[] = {arg(0), arg(1), 0, data, arg(6), signed_word(7), signed_word(8), signed_word(9),
                              signed_word(10), flags};
        result = Api("DrawStateA", a);
        return true;
    }
    case 185: // GrayString(hdc, brush, procedure, data, count, x, y, cx, cy)
    {
        // Without a procedure the data is the far string. With one, USER
        // calls the program's procedure into a memory DC of its own; the
        // native entry would call it with the frame of a hook.
        if (dword(2))
        {
            Fail("Win16 gate USER.185 GrayString with an output procedure is not carried");
            return false;
        }
        const unsigned a[] = {arg(0), arg(1), 0, pointer(4), signed_word(6), signed_word(7), signed_word(8),
                              signed_word(9), signed_word(10)};
        result = Api("GrayStringA", a);
        return true;
    }

    // ---- deferred positioning: the handle is a dword natively
    case 259: { const unsigned a[] = {signed_word(0)}; result = defer_word(Api("BeginDeferWindowPos", a)); return true; }
    case 260: // DeferWindowPos(hdwp, hwnd, after, x, y, cx, cy, flags)
    {
        const unsigned native = defer_native(arg(0));
        const unsigned a[] = {native, arg(1), arg(2), signed_word(3), signed_word(4), signed_word(5), signed_word(6),
                              arg(7)};
        result = native ? defer_word(Api("DeferWindowPos", a)) : 0u;
        return true;
    }
    case 261:
    {
        const unsigned native = defer_native(arg(0));
        const unsigned a[] = {native};
        result = native ? Api("EndDeferWindowPos", a) : 0u;
        return true;
    }

    // ---- the window enumerations, answered through the far callback
    case 54:  // EnumWindows(procedure, parameter)
    case 55:  // EnumChildWindows(parent, procedure, parameter)
    case 225: // EnumTaskWindows(task, procedure, parameter)
    {
        const unsigned first = ordinal == 54 ? 0 : 1;
        const unsigned procedure = dword(first), parameter = dword(first + 2);
        result = 0;
        if (!procedure)
            return true;
        // The windows first, the calls afterwards, as USER32 builds its
        // list before the first callback: a window the callback makes or
        // destroys does not disturb the walk.
        static constexpr unsigned Limit = 1024;
        unsigned list[Limit], count = 0;
        const unsigned desktop_args[] = {};
        const unsigned desktop = Api("GetDesktopWindow", desktop_args);
        if (ordinal == 55)
        {
            // Depth first, every descendant of the parent.
            unsigned stack[64], depth = 0;
            const unsigned child_args[] = {arg(0), 5}; // GW_CHILD
            unsigned hwnd = arg(0) && User::IsWindow(arg(0)) ? Api("GetWindow", child_args) : 0u;
            while (hwnd && count < Limit)
            {
                list[count++] = hwnd;
                const unsigned down[] = {hwnd, 5};
                const unsigned child = Api("GetWindow", down);
                if (child && depth < 64)
                {
                    stack[depth++] = hwnd;
                    hwnd = child;
                    continue;
                }
                const unsigned next_args[] = {hwnd, 2}; // GW_HWNDNEXT
                hwnd = Api("GetWindow", next_args);
                while (!hwnd && depth)
                {
                    const unsigned up[] = {stack[--depth], 2};
                    hwnd = Api("GetWindow", up);
                }
            }
        }
        else
        {
            const unsigned top_args[] = {desktop, 5};
            for (unsigned hwnd = Api("GetWindow", top_args); hwnd && count < Limit;)
            {
                bool take = true;
                if (ordinal == 225)
                {
                    // A task is the thread GetWindowTask (USER.224) names, or
                    // the task KERNEL.36 hands out for the caller's own.
                    const unsigned owner_args[] = {hwnd, 0};
                    const unsigned thread = Api("GetWindowThreadProcessId", owner_args);
                    take = (thread & 0xffff) == arg(0) ||
                           (win16.state && arg(0) == win16.state->task && thread == User::CurrentThread());
                }
                if (take)
                    list[count++] = hwnd;
                const unsigned next_args[] = {hwnd, 2};
                hwnd = Api("GetWindow", next_args);
            }
        }
        result = 1;
        for (unsigned i = 0; i < count && state != Failed && state != Exited; ++i)
        {
            if (!User::IsWindow(list[i]))
                continue;
            const unsigned push[] = {list[i] & 0xffff, parameter >> 16, parameter & 0xffff};
            if (!(Win16CallPascal(procedure, push, 3, ~0u) & 0xffff))
            {
                result = 0;
                break;
            }
        }
        return state != Failed;
    }
    default:
        break;
    }
    handled = false;
    return false;
}
