// SPDX-License-Identifier: GPL-2.0-or-later
// Three edit controls for the context menu of USER
// (tools/test_edit_menu_host.py): a plain one, a read-only one and one with a
// password character, in a window without a frame at the corner of the
// screen. Every 50 ms the program compares the text and the selection of
// each and the text of the clipboard with what it wrote last and appends a
// line to C:\EDITMENU.TXT when one of them changed.
#include <windows.h>

static HWND edits[3];
static char last[512];

static void record(void)
{
    char now[512], text[64], clip[64] = "";
    int used = 0, i;
    DWORD written;
    HANDLE file, data;
    if (OpenClipboard(0))
    {
        data = GetClipboardData(CF_TEXT);
        if (data)
        {
            lstrcpynA(clip, (const char *)GlobalLock(data), sizeof clip);
            GlobalUnlock(data);
        }
        CloseClipboard();
    }
    for (i = 0; i < 3; ++i)
    {
        DWORD start = 0, end = 0;
        GetWindowTextA(edits[i], text, sizeof text);
        SendMessageA(edits[i], EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
        used += wsprintfA(now + used, "%d=[%s] %lu-%lu ", i, text, start, end);
    }
    used += wsprintfA(now + used, "clip=[%s] focus=%d\r\n", clip,
                      GetFocus() == edits[0] ? 0 : GetFocus() == edits[1] ? 1 : GetFocus() == edits[2] ? 2 : -1);
    if (!lstrcmpA(now, last))
        return;
    lstrcpyA(last, now);
    file = CreateFileA("C:\\EDITMENU.TXT", GENERIC_WRITE, 0, 0, OPEN_ALWAYS, 0, 0);
    SetFilePointer(file, 0, 0, FILE_END);
    WriteFile(file, now, used, &written, 0);
    CloseHandle(file);
}
static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_TIMER)
        record();
    if (message == WM_DESTROY)
        PostQuitMessage(0);
    return DefWindowProcA(window, message, wparam, lparam);
}

void mainCRTStartup(void)
{
    static const char *const texts[3] = {"hello world", "read only", "secret"};
    static const DWORD styles[3] = {ES_AUTOHSCROLL, ES_AUTOHSCROLL | ES_READONLY, ES_AUTOHSCROLL | ES_PASSWORD};
    WNDCLASSA c = {0};
    MSG message;
    HWND window;
    int i;
    c.lpfnWndProc = procedure;
    c.hInstance = GetModuleHandleA(0);
    c.lpszClassName = "EditMenu";
    c.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    c.hCursor = LoadCursorA(0, IDC_ARROW);
    RegisterClassA(&c);
    window = CreateWindowExA(0, c.lpszClassName, "Edit menu", WS_POPUP | WS_VISIBLE, 0, 0, 400, 200, 0, 0,
                             c.hInstance, 0);
    for (i = 0; i < 3; ++i)
        edits[i] = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", texts[i], WS_CHILD | WS_VISIBLE | WS_TABSTOP | styles[i],
                                   10, 10 + i * 40, 300, 24, window, (HMENU)(100 + i), c.hInstance, 0);
    if (OpenClipboard(window))
    {
        EmptyClipboard();
        CloseClipboard();
    }
    SetForegroundWindow(window);
    record();
    SetTimer(window, 1, 50, 0);
    while (GetMessageA(&message, 0, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    ExitProcess(0);
}
