/* SPDX-License-Identifier: GPL-2.0-or-later
 * EM_GETRECT after EM_SETRECT(NULL) for a bordered multiline EDIT child. */
#include <windows.h>
#include "wine/test.h"

START_TEST(edit_default_rect)
{
    HWND parent, edit;
    RECT r, client;
    parent = CreateWindowExA(0, "STATIC", "", WS_POPUP | WS_VISIBLE, 0, 0, 400, 300, NULL, NULL, GetModuleHandleA(NULL), NULL);
    edit = CreateWindowExA(0, "EDIT", "", 0x50800084, 10, 10, 240, 120, parent, NULL, GetModuleHandleA(NULL), NULL);
    ok(parent && edit, "windows\n");
    SendMessageA(edit, WM_SETTEXT, 0, (LPARAM)"abcdef");
    GetClientRect(edit, &client);
    SendMessageA(edit, EM_GETRECT, 0, (LPARAM)&r);
    trace("client %ld,%ld initial rect %ld,%ld,%ld,%ld margins=%08lx\n", client.right, client.bottom, r.left, r.top, r.right, r.bottom,
          (unsigned long)SendMessageA(edit, EM_GETMARGINS, 0, 0));
    RECT before;
    r.left = 30; r.top = 20; r.right = 100; r.bottom = 50;
    SendMessageA(edit, EM_SETRECT, 0, (LPARAM)&r);
    SendMessageA(edit, EM_GETRECT, 0, (LPARAM)&before);
    SendMessageA(edit, EM_SETRECT, 0, 0);
    SendMessageA(edit, EM_GETRECT, 0, (LPARAM)&r);
    trace("set %ld,%ld,%ld,%ld after EM_SETRECT(NULL): %ld,%ld,%ld,%ld\n", before.left, before.top, before.right,
          before.bottom, r.left, r.top, r.right, r.bottom);
    ok(EqualRect(&r, &before), "EM_SETRECT(NULL) leaves the formatting rectangle unchanged\n");
    DestroyWindow(parent);
}
