/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 *
 * File Manager's Run (tools/test_winfile_run_host.py): starts WINFILE.EXE,
 * chooses File > Run from its menu and looks at the Command Line field of
 * the dialog: its length, its selection (EM_GETSEL), and its length after
 * one typed character. Then it runs Calculator through that dialog twice,
 * typed as "calc" and as "c:\windows\calc.exe" and confirmed with OK, and
 * notes whether its window came. C:\WFRUN.TXT (and COM1, when it opens)
 * gets name=value lines; nothing of this process's memory crosses to the
 * other, the characters go as WM_CHAR.
 */
#include <windows.h>

static HANDLE out = INVALID_HANDLE_VALUE, com = INVALID_HANDLE_VALUE;
static void put(const char *name, DWORD value)
{
    char line[100];
    DWORD n;
    wsprintfA(line, "%s=%lu\r\n", name, value);
    WriteFile(out, line, lstrlenA(line), &n, 0);
    if (com != INVALID_HANDLE_VALUE)
        WriteFile(com, line, lstrlenA(line), &n, 0);
}
static HWND frame, run_item_frame;
static UINT run_item;
static HWND wait_for(const char *klass, const char *title);
/* Run of File Manager with `text` typed into its field and OK: 1 when the
   window of Calculator came. */
static DWORD run_calculator(const char *text)
{
    HWND dialog, field, calc;
    int i;
    PostMessageA(frame, WM_COMMAND, run_item, 0);
    dialog = wait_for("#32770", "Run");
    if (!dialog)
        return 0;
    Sleep(500);
    field = FindWindowExA(dialog, 0, "Edit", 0);
    if (!field)
        return 0;
    SendMessageA(field, EM_SETSEL, 0, -1);
    for (i = 0; text[i]; i++)
        SendMessageA(field, WM_CHAR, (unsigned char)text[i], 0);
    PostMessageA(dialog, WM_COMMAND, IDOK, 0);
    calc = wait_for("SciCalc", 0);
    if (calc) {
        PostMessageA(calc, WM_CLOSE, 0, 0);
        for (i = 0; i < 100 && IsWindow(calc); i++)
            Sleep(100);
    }
    return calc != 0;
}
static HWND wait_for(const char *klass, const char *title)
{
    int i;
    for (i = 0; i < 300; i++) {
        HWND w = FindWindowA(klass, title);
        if (w && IsWindowVisible(w))
            return w;
        Sleep(100);
    }
    return 0;
}
void mainCRTStartup(void)
{
    HWND dialog = 0, field = 0;
    HMENU menu, file;
    int i, count;
    DWORD selection;
    out = CreateFileA("C:\\WFRUN.TXT", GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
    com = CreateFileA("COM1", GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
    WinExec("C:\\WINDOWS\\WINFILE.EXE", SW_SHOWNORMAL);
    frame = wait_for("WFS_Frame", 0);
    put("frame", frame != 0);
    if (frame) {
        Sleep(2000);
        menu = GetMenu(frame);
        file = GetSubMenu(menu, 0);
        count = GetMenuItemCount(file);
        for (i = 0; i < count; i++) {
            char text[64];
            text[0] = 0;
            GetMenuStringA(file, i, text, sizeof text, MF_BYPOSITION);
            if (text[0] == '&' ? text[1] == 'R' && text[2] == 'u' : text[0] == 'R' && text[1] == 'u')
                run_item = GetMenuItemID(file, i);
        }
        if (run_item)
            PostMessageA(frame, WM_COMMAND, run_item, 0);
        dialog = wait_for("#32770", "Run");
    }
    put("dialog", dialog != 0);
    if (dialog) {
        Sleep(1000);
        field = FindWindowExA(dialog, 0, "Edit", 0);
        put("field", field != 0);
        put("length", field ? (DWORD)SendMessageA(field, WM_GETTEXTLENGTH, 0, 0) : 0);
        selection = field ? (DWORD)SendMessageA(field, EM_GETSEL, 0, 0) : 0;
        put("selection.start", LOWORD(selection));
        put("selection.end", HIWORD(selection));
        put("focus", GetFocus() == field);
        if (field) {
            SendMessageA(field, WM_CHAR, 'x', 0);
            put("typed.length", (DWORD)SendMessageA(field, WM_GETTEXTLENGTH, 0, 0));
        }
        PostMessageA(dialog, WM_COMMAND, IDCANCEL, 0);
        Sleep(1000);
        put("started by name", run_calculator("calc"));
        put("started by path", run_calculator("c:\\windows\\calc.exe"));
    }
    if (frame)
        PostMessageA(frame, WM_CLOSE, 0, 0);
    put("done", 1);
    if (com != INVALID_HANDLE_VALUE)
        WriteFile(com, "MEASURE_DONE\r\n", 14, &selection, 0);
    CloseHandle(out);
    ExitProcess(0);
}
