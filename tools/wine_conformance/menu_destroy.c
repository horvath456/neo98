/* SPDX-License-Identifier: GPL-2.0-or-later
 * Does DestroyWindow destroy the window's menu? Top-level window and dialog. */
#include <windows.h>
#include "wine/test.h"

static INT_PTR CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) { return m == WM_INITDIALOG; }

START_TEST(menu_destroy)
{
    WNDCLASSA cls = {0};
    HWND window, dialog;
    HMENU menu, second, popup;
    struct { DLGTEMPLATE t; WORD menu, cls, title; } tpl = {{WS_POPUP | WS_CAPTION, 0, 0, 0, 0, 100, 50}, 0, 0, 0};
    cls.lpfnWndProc = DefWindowProcA; cls.hInstance = GetModuleHandleA(NULL); cls.lpszClassName = "MenuDestroy98";
    ok(RegisterClassA(&cls) != 0, "class\n");
    menu = CreateMenu(); popup = CreatePopupMenu();
    ok(AppendMenuA(popup, MF_STRING, 1, "item") && AppendMenuA(menu, MF_POPUP, (UINT_PTR)popup, "&File"), "build menu\n");
    window = CreateWindowExA(0, "MenuDestroy98", "w", WS_OVERLAPPEDWINDOW, 0, 0, 200, 150, 0, menu, cls.hInstance, 0);
    ok(window && GetMenu(window) == menu, "window owns menu\n");
    ok(DestroyWindow(window), "destroy window\n");
    trace("after DestroyWindow: IsMenu(menu)=%d IsMenu(popup)=%d\n", IsMenu(menu), IsMenu(popup));
    ok(!IsMenu(menu), "top-level menu destroyed with its window\n");
    ok(!IsMenu(popup), "its popup destroyed too\n");
    second = CreateMenu();
    trace("next CreateMenu=%p previous=%p\n", second, menu);
    DestroyMenu(second);
    /* A dialog whose menu was set at run time. */
    dialog = CreateDialogIndirectParamA(cls.hInstance, &tpl.t, 0, proc, 0);
    ok(dialog != NULL, "dialog\n");
    menu = CreateMenu();
    ok(SetMenu(dialog, menu), "set dialog menu\n");
    ok(DestroyWindow(dialog), "destroy dialog\n");
    trace("dialog: IsMenu(menu)=%d\n", IsMenu(menu));
    ok(!IsMenu(menu), "dialog menu destroyed with the dialog\n");
}
