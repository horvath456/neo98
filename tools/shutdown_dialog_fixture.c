/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 *
 * Where SHELL32's Shut Down dialog stands (tools/test_shutdown_dialog_host.py
 * takes the numbers it measured in Windows 95 and 98): the program opens
 * the Start menu with Ctrl+Esc and chooses Shut Down with U, and a thread of
 * its own finds Explorer's dialog "Shut Down Windows", writes the work area,
 * the dialog's rectangle and those of its visible children (id and
 * rectangle), and closes it with its button 2
 * (Cancel in Windows 98, No in Windows 95). Lines go to C:\SHUTDLG.TXT and,
 * when it opens, to COM1, the last one MEASURE_DONE.
 */
#include <windows.h>

static HANDLE out = INVALID_HANDLE_VALUE, serial = INVALID_HANDLE_VALUE;
static void put(const char *s)
{
    DWORD n;
    WriteFile(out, s, lstrlenA(s), &n, 0);
    if (serial != INVALID_HANDLE_VALUE)
        WriteFile(serial, s, lstrlenA(s), &n, 0);
}
static BOOL CALLBACK child(HWND window, LPARAM unused)
{
    char line[200];
    RECT r;
    if (!IsWindowVisible(window))
        return TRUE;
    GetWindowRect(window, &r);
    wsprintfA(line, "child %d %ld,%ld,%ld,%ld\r\n", GetDlgCtrlID(window), r.left, r.top, r.right, r.bottom);
    put(line);
    return TRUE;
}
static DWORD WINAPI watcher(LPVOID unused)
{
    char line[200];
    RECT r, work;
    HWND dialog = 0;
    DWORD start = GetTickCount();
    while (!dialog && GetTickCount() - start < 30000) {
        Sleep(200);
        dialog = FindWindowA("#32770", "Shut Down Windows");
    }
    Sleep(1500);
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
    wsprintfA(line, "screen %d,%d work %ld,%ld,%ld,%ld\r\n", GetSystemMetrics(SM_CXSCREEN),
              GetSystemMetrics(SM_CYSCREEN), work.left, work.top, work.right, work.bottom);
    put(line);
    if (dialog) {
        GetWindowRect(dialog, &r);
        wsprintfA(line, "dialog %ld,%ld,%ld,%ld\r\n", r.left, r.top, r.right, r.bottom);
        put(line);
        EnumChildWindows(dialog, child, 0);
        PostMessageA(dialog, WM_COMMAND, 2, (LPARAM)GetDlgItem(dialog, 2));
    } else
        put("dialog none\r\n");
    return 0;
}
static void key(BYTE code, DWORD flags)
{
    keybd_event(code, (BYTE)MapVirtualKeyA(code, 0), flags, 0);
    Sleep(100);
}
void mainCRTStartup(void)
{
    HANDLE thread;
    out = CreateFileA("C:\\SHUTDLG.TXT", GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
    serial = CreateFileA("COM1", GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
    /* Explorer's own way: the Start menu (Ctrl+Esc), then Shut Down (U). */
    Sleep(3000);
    key(VK_CONTROL, 0);
    key(VK_ESCAPE, 0);
    key(VK_ESCAPE, KEYEVENTF_KEYUP);
    key(VK_CONTROL, KEYEVENTF_KEYUP);
    Sleep(1500);
    key('U', 0);
    key('U', KEYEVENTF_KEYUP);
    thread = CreateThread(0, 0, watcher, 0, 0, 0);
    WaitForSingleObject(thread, 60000);
    CloseHandle(thread);
    Sleep(1000);
    put("done\r\nMEASURE_DONE\r\n");
    CloseHandle(out);
    ExitProcess(0);
}
