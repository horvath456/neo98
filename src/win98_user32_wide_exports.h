// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_clipboard_exports.h"
// These wide USER32 exports are unsupported on Windows 98. Return zero, set
// ERROR_CALL_NOT_IMPLEMENTED (120), and remove the recorded argument count.
// WSPRINTFW uses cdecl and leaves its arguments for the caller.
static const UserUnsupportedExport OriginalUser32WideUnsupportedExports[] = {
    // Window stations and desktops exist only as NT concepts; Win98 fails all
    // of them, including the two ANSI entries.
    {"GetProcessWindowStation", 306, 0},  //
    {"CloseDesktop", 64, 1},              //
    {"CloseWindowStation", 66, 1},        //
    {"SetProcessWindowStation", 546, 1},  //
    {"OpenWindowStationA", 456, 3},       //
    {"SetUserObjectSecurity", 562, 3},    //
    {"OpenDesktopA", 452, 4},             //
    {"GetUserObjectInformationW", 328, 5},  //
    // Messages and windows.
    {"DispatchMessageW", 148, 1},          //
    {"RegisterClipboardFormatW", 477, 1},  //
    {"RegisterWindowMessageW", 486, 1},    //
    {"CallMsgFilterW", 21, 2},             //
    {"FindWindowW", 214, 2},               //
    {"GetPropW", 308, 2},                  //
    {"GetWindowLongW", 335, 2},            //
    {"IsDialogMessageW", 385, 2},          //
    {"RemovePropW", 491, 2},               //
    {"SetPropW", 548, 3},                  //
    {"SetWindowLongW", 567, 3},            //
    {"DefDlgProcW", 128, 4},               //
    {"DefWindowProcW", 134, 4},            //
    {"FindWindowExW", 213, 4},             //
    {"GetMessageW", 295, 4},               //
    {"PostMessageW", 464, 4},              //
    {"SendMessageW", 508, 4},              //
    {"SetWindowsHookExW", 576, 4},         //
    {"SystemParametersInfoW", 590, 4},     //
    {"WinHelpW", 637, 4},                  //
    {"CallWindowProcW", 24, 5},            //
    {"CreateDialogParamW", 82, 5},         //
    {"DialogBoxParamW", 146, 5},           //
    {"PeekMessageW", 461, 5},              //
    {"SendDlgItemMessageW", 499, 5},       //
    {"SendMessageTimeoutW", 507, 7},       //
    {"CreateWindowExW", 92, 12},           //
    // Resources: menus, accelerators and strings.
    {"CreateAcceleratorTableW", 74, 2},   //
    {"LoadAcceleratorsW", 398, 2},        //
    {"LoadMenuW", 414, 2},                //
    {"CopyAcceleratorTableW", 68, 3},     //
    {"TranslateAcceleratorW", 604, 3},    //
    {"LoadStringW", 416, 4},              //
    // Keyboard and characters.
    {"VkKeyScanW", 629, 1},     //
    {"CharToOemW", 49, 2},      //
    {"MapVirtualKeyW", 425, 2}, //
    {"OemToCharW", 449, 2},     //
    {"GetKeyNameTextW", 267, 3},//
    // Text output and formatting.
    {"wsprintfW", 646, 0},      // cdecl, no argument removal
    {"wvsprintfW", 648, 3},     //
    {"DrawTextW", 176, 5},      //
    {"DrawTextExW", 175, 6},    //
    // Display settings.
    {"ChangeDisplaySettingsExW", 30, 5},  //
};
