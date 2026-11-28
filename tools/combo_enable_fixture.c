/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 *
 * A drop-down combo box disabled and enabled again with EnableWindow
 * (tools/test_combo_enable_host.py): whether its edit field follows, and
 * which colour message the parent gets when the field paints (Windows 95
 * and later: WM_CTLCOLORSTATIC for a disabled or read-only field,
 * WM_CTLCOLOREDIT for one that takes input). C:\COMBOEN.TXT (and COM1,
 * when it opens) gets name=value lines.
 */
#include <windows.h>

static HANDLE out = INVALID_HANDLE_VALUE, com = INVALID_HANDLE_VALUE;
static int edit_colour, static_colour;
static void put(const char *name, DWORD value)
{
    char line[100];
    DWORD n;
    wsprintfA(line, "%s=%lu\r\n", name, value);
    WriteFile(out, line, lstrlenA(line), &n, 0);
    if (com != INVALID_HANDLE_VALUE)
        WriteFile(com, line, lstrlenA(line), &n, 0);
}
static LRESULT CALLBACK parent_procedure(HWND w, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_CTLCOLOREDIT)
        edit_colour++;
    if (message == WM_CTLCOLORSTATIC)
        static_colour++;
    return DefWindowProcA(w, message, wp, lp);
}
static void paint(HWND edit)
{
    MSG m;
    edit_colour = static_colour = 0;
    InvalidateRect(edit, 0, TRUE);
    UpdateWindow(edit);
    while (PeekMessageA(&m, 0, 0, 0, PM_REMOVE))
        DispatchMessageA(&m);
}
void mainCRTStartup(void)
{
    WNDCLASSA klass = {0};
    HWND parent, combo, edit;
    MSG m;
    out = CreateFileA("C:\\COMBOEN.TXT", GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
    com = CreateFileA("COM1", GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
    klass.lpfnWndProc = parent_procedure;
    klass.hInstance = GetModuleHandleA(0);
    klass.lpszClassName = "ComboEnableParent";
    klass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassA(&klass);
    parent = CreateWindowA("ComboEnableParent", "Combo", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 20, 20, 320, 200, 0, 0,
                           klass.hInstance, 0);
    combo = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | WS_VSCROLL, 10, 10, 200, 120, parent,
                          (HMENU)100, klass.hInstance, 0);
    SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM)"(None)");
    SendMessageA(combo, CB_SETCURSEL, 0, 0);
    edit = FindWindowExA(combo, 0, "Edit", 0);
    while (PeekMessageA(&m, 0, 0, 0, PM_REMOVE))
        DispatchMessageA(&m);
    put("edit", edit != 0);
    paint(edit);
    put("enabled.edit", IsWindowEnabled(edit) != 0);
    put("enabled.ctlcoloredit", edit_colour);
    put("enabled.ctlcolorstatic", static_colour);
    EnableWindow(combo, FALSE);
    paint(edit);
    put("disabled.combo", IsWindowEnabled(combo) != 0);
    put("disabled.edit", IsWindowEnabled(edit) != 0);
    put("disabled.ctlcoloredit", edit_colour);
    put("disabled.ctlcolorstatic", static_colour);
    EnableWindow(combo, TRUE);
    paint(edit);
    put("again.edit", IsWindowEnabled(edit) != 0);
    put("again.ctlcoloredit", edit_colour);
    put("again.ctlcolorstatic", static_colour);
    put("done", 1);
    if (com != INVALID_HANDLE_VALUE)
        WriteFile(com, "MEASURE_DONE\r\n", 14, (DWORD *)&edit_colour, 0);
    CloseHandle(out);
    ExitProcess(0);
}
