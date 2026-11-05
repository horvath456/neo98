/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest for keys sent to a ComboBox. USER hands WM_KEYDOWN
   of a closed ComboBox to its edit field when it has one (to its list
   otherwise), 05D4 does the same for WM_CHAR, and the ComboBox answers what
   that control answered. Both bodies of the EDIT procedure answer WM_KEYDOWN
   and WM_CHAR with 1. SHELL32's Run dialog sends
   WM_KEYDOWN with Enter to its ComboBox on OK and starts the program only on
   a non-zero answer. The focus helper Windows 98 selects the whole text of the
   edit field when the ComboBox takes the focus. Exit code zero means every
   check held; codes 4850..4860. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U SendMessageA(U, U, U, U);
API U GetWindow(U, U);
API U GetWindowTextA(U, char *, int);
API U DestroyWindow(U);
API U SetFocus(U);

static U failure;
static void check(int ok, U code)
{
    if (!ok && !failure)
        failure = code;
}
void mainCRTStartup(void)
{
    U parent = CreateWindowExA(0, "STATIC", "", 0x90000000, 0, 0, 300, 200, 0, 0, 0, 0);
    check(parent != 0, 4850);
    /* CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP | WS_CHILD | WS_VISIBLE, as in the Run dialog */
    U combo = CreateWindowExA(0, "COMBOBOX", "", 0x50210842, 10, 10, 200, 160, parent, 0x300a, 0, 0);
    check(combo != 0, 4851);
    U edit = GetWindow(combo, 5);
    check(edit != 0, 4852);
    check(SendMessageA(combo, 0x100, 0x0d, 0x001c0001) == 1, 4853);
    check(SendMessageA(edit, 0x100, 0x27, 0x014d0001) == 1, 4854);
    check(SendMessageA(edit, 0x102, 'a', 0x001e0001) == 1, 4855);
    check(SendMessageA(combo, 0x102, 'b', 0x00300001) == 1, 4856);
    char text[16] = {0};
    GetWindowTextA(edit, text, sizeof text);
    check(text[0] == 'a' && text[1] == 'b' && text[2] == 0, 4857);
    /* The focus selects the whole text: EM_GETSEL answers start 0, end 2. */
    SendMessageA(edit, 0xb1, 1, 1);
    SetFocus(combo);
    check(SendMessageA(edit, 0xb0, 0, 0) == (2u << 16), 4860);
    /* CBS_DROPDOWNLIST has no edit field; its keys go to the list. */
    U list = CreateWindowExA(0, "COMBOBOX", "", 0x50210843, 10, 40, 200, 160, parent, 0x300b, 0, 0);
    check(list != 0, 4858);
    SendMessageA(list, 0x143, 0, (U)"one");
    SendMessageA(list, 0x143, 0, (U)"two");
    SendMessageA(list, 0x14e, 0, 0);
    SendMessageA(list, 0x100, 0x28, 0x01500001);
    check(SendMessageA(list, 0x147, 0, 0) == 1, 4859);
    DestroyWindow(parent);
    ExitProcess(failure);
}
