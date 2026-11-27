// SPDX-License-Identifier: GPL-2.0-or-later
// Text typed into a combo box is no item of its list
// (tools/test_combo_typed_host.py).
//
// When the user changes the text of a drop-down combo box, USER tells the
// parent (CBN_EDITCHANGE) and then lets the list follow the text: no item is
// selected any more, so CB_GETCURSEL answers CB_ERR until an item is chosen
// again. A dialog that reads "the selected item, or else the text" depends
// on that: the separator boxes of Regional Settings stored the old item
// when a new separator had been typed.
// The program types into the edit field of a combo box with three items.
// C:\COMBOTYP.TXT gets one line "name=value" per finding.
#include <windows.h>

static char text[1024];
static int used, changes;

static void line(const char *name, long value)
{
    used += wsprintfA(text + used, "%s=%ld\r\n", name, value);
}
static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_COMMAND && HIWORD(wp) == CBN_EDITCHANGE) ++changes;
    return DefWindowProcA(window, message, wp, lp);
}
static void type(HWND edit, char character)
{
    SendMessageA(edit, EM_SETSEL, 0, -1);
    SendMessageA(edit, WM_CHAR, (WPARAM)character, 0);
}

void mainCRTStartup(void)
{
    WNDCLASSA c = {0};
    HWND parent, combo, edit;
    char now[16];
    HANDLE file;
    DWORD written = 0;
    c.lpfnWndProc = procedure;
    c.hInstance = GetModuleHandleA(0);
    c.lpszClassName = "ComboTyped";
    RegisterClassA(&c);
    parent = CreateWindowExA(0, "ComboTyped", "Combo", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, 300, 200, 0, 0,
                             c.hInstance, 0);
    combo = CreateWindowExA(0, "COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWN, 20, 20, 120, 120, parent,
                            (HMENU)100, c.hInstance, 0);
    SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM) ":");
    SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM) ".");
    SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM) "-");
    SendMessageA(combo, CB_SETCURSEL, 0, 0);
    line("selected at first", SendMessageA(combo, CB_GETCURSEL, 0, 0));
    edit = GetWindow(combo, GW_CHILD);
    line("edit field found", edit != 0);
    type(edit, '.'); // the text of the second item
    line("selected after typing an item's text", SendMessageA(combo, CB_GETCURSEL, 0, 0));
    GetWindowTextA(combo, now, sizeof now);
    line("text is what was typed", !lstrcmpA(now, "."));
    line("CBN_EDITCHANGE", changes);
    type(edit, 'x'); // no item
    line("selected after typing other text", SendMessageA(combo, CB_GETCURSEL, 0, 0));
    SendMessageA(combo, CB_SETCURSEL, 2, 0);
    line("selected after choosing again", SendMessageA(combo, CB_GETCURSEL, 0, 0));
    GetWindowTextA(combo, now, sizeof now);
    line("text is the chosen item", !lstrcmpA(now, "-"));
    file = CreateFileA("C:\\COMBOTYP.TXT", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    WriteFile(file, text, used, &written, 0);
    CloseHandle(file);
    ExitProcess(0);
}
