/* SPDX-License-Identifier: GPL-2.0-or-later
 * CreateWindowEx with WS_CHILD and no parent window. */
#include <windows.h>
#include "wine/test.h"

START_TEST(child_no_parent)
{
    HWND edit, plain;
    SetLastError(0xdeadbeef);
    edit = CreateWindowExA(0, "EDIT", "x", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE, 10, 10, 200, 100, NULL, NULL, GetModuleHandleA(NULL), NULL);
    trace("WS_CHILD EDIT without parent: %p error=%lu\n", edit, GetLastError());
    ok(edit == NULL, "WS_CHILD without a parent fails\n");
    if (edit) DestroyWindow(edit);
    plain = CreateWindowExA(0, "EDIT", "x", WS_VISIBLE | WS_BORDER | ES_MULTILINE, 10, 10, 200, 100, NULL, NULL, GetModuleHandleA(NULL), NULL);
    ok(plain != NULL, "top-level EDIT\n");
    if (plain) DestroyWindow(plain);
}
