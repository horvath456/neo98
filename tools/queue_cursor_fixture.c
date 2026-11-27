/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The cursor of the queue under the pointer (tools/test_queue_cursor_host.py).

   Window A (0,0 300x200) belongs to the main thread and sets the cross on
   WM_SETCURSOR only when GetCursor answers another cursor, as Internet
   Explorer does over a link. Window B (300,0 300x200) belongs to a second
   thread and has the arrow of its class. Each WM_MOUSEMOVE in A asks
   GetCursorInfo which cursor the pointer shows; A's title counts the moves
   and those that did not show the cross: "moves N wrong M". */
#define WINVER 0x0500
#include <windows.h>

static HCURSOR cross;
static int moves, wrong;
static char title[48];

static LRESULT CALLBACK a_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_SETCURSOR && LOWORD(l) == HTCLIENT)
    {
        if (GetCursor() != cross) SetCursor(cross);
        return TRUE;
    }
    if (m == WM_MOUSEMOVE)
    {
        CURSORINFO info;
        info.cbSize = sizeof info;
        ++moves;
        if (!GetCursorInfo(&info) || info.hCursor != cross) ++wrong;
        wsprintfA(title, "moves %d wrong %d", moves, wrong);
        SetWindowTextA(h, title);
        return 0;
    }
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
}

static DWORD WINAPI b_thread(LPVOID instance)
{
    CreateWindowA("QueueCursorB", "B", WS_POPUP | WS_BORDER | WS_VISIBLE, 300, 0, 300, 200, 0, 0,
                  (HINSTANCE)instance, 0);
    MSG message;
    while (GetMessageA(&message, 0, 0, 0)) DispatchMessageA(&message);
    return 0;
}

void mainCRTStartup(void)
{
    HINSTANCE instance = GetModuleHandleA(0);
    cross = LoadCursorA(0, (LPCSTR)IDC_CROSS);
    WNDCLASSA k = {0};
    k.hInstance = instance; k.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    k.lpfnWndProc = a_proc; k.lpszClassName = "QueueCursorA";
    RegisterClassA(&k);
    k.lpfnWndProc = DefWindowProcA; k.hCursor = LoadCursorA(0, (LPCSTR)IDC_ARROW); k.lpszClassName = "QueueCursorB";
    RegisterClassA(&k);
    CreateWindowA("QueueCursorA", "moves 0 wrong 0", WS_POPUP | WS_BORDER | WS_VISIBLE, 0, 0, 300, 200, 0, 0,
                  instance, 0);
    DWORD id;
    CloseHandle(CreateThread(0, 0, b_thread, instance, 0, &id));
    MSG message;
    while (GetMessageA(&message, 0, 0, 0)) DispatchMessageA(&message);
    ExitProcess(0);
}
