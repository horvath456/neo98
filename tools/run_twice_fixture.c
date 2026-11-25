/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Started from Start > Run as "C:\RUNTWICE.EXE N", it shows a window
   titled "Run twice N". The first one also checks how a WM_QUIT that
   PostThreadMessage put into the queue meets a filter, and writes one line
   per check into C:\RUNTWICE.TXT before its window shows:

     range    with 402h and WM_QUIT posted, PeekMessage(402h..402h,
              PM_REMOVE) takes 402h and then nothing, and PeekMessage
              without a filter then takes WM_QUIT
     window   a WM_QUIT posted to the thread does not come to a
              PeekMessage for a window, only to one without
     thread   a thread that waits as the autocomplete of BROWSEUI waits:
              it takes each message, drains the further 402h with
              PeekMessage(402h..402h, PM_REMOVE), and ends at WM_QUIT; it
              ends within five seconds of 402h and WM_QUIT posted to it

   tools/test_run_twice_host.py. */
#include <windows.h>

static HANDLE out;

static void put(const char *name, int value)
{
    char line[64];
    int n = wsprintfA(line, "%s=%d\r\n", name, value);
    DWORD written;
    WriteFile(out, line, n, &written, 0);
}

static DWORD WINAPI waiter(void *ready)
{
    MSG msg;
    PeekMessageA(&msg, 0, 0, 0, PM_NOREMOVE); /* the queue of the thread */
    SetEvent(ready);
    for (;;)
    {
        if (!PeekMessageA(&msg, 0, 0, 0xffffffff, PM_NOREMOVE))
        {
            MsgWaitForMultipleObjects(0, 0, FALSE, 60000, QS_ALLINPUT);
            continue;
        }
        GetMessageA(&msg, 0, 0, 0);
        if (msg.message == WM_QUIT)
            return 0;
        if (msg.message == 0x402)
            while (PeekMessageA(&msg, msg.hwnd, 0x402, 0x402, PM_REMOVE))
                ;
    }
}

static void checks(HWND window)
{
    out = CreateFileA("C:\\RUNTWICE.TXT", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    const DWORD self = GetCurrentThreadId();
    MSG msg;
    PostThreadMessageA(self, 0x402, 0, 0);
    PostThreadMessageA(self, WM_QUIT, 7, 0);
    const BOOL first = PeekMessageA(&msg, 0, 0x402, 0x402, PM_REMOVE) && msg.message == 0x402;
    const BOOL second = PeekMessageA(&msg, 0, 0x402, 0x402, PM_REMOVE);
    const BOOL quit = PeekMessageA(&msg, 0, 0, 0, PM_REMOVE) && msg.message == WM_QUIT && msg.wParam == 7;
    put("range", first && !second && quit);

    PostThreadMessageA(self, WM_QUIT, 9, 0);
    const BOOL windowed = PeekMessageA(&msg, window, 0, 0, PM_REMOVE);
    const BOOL any = PeekMessageA(&msg, 0, 0, 0, PM_REMOVE) && msg.message == WM_QUIT && msg.wParam == 9;
    put("window", !windowed && any);

    HANDLE ready = CreateEventA(0, TRUE, FALSE, 0);
    DWORD id;
    HANDLE thread = CreateThread(0, 0, waiter, ready, 0, &id);
    WaitForSingleObject(ready, 5000);
    Sleep(100);
    PostThreadMessageA(id, 0x402, 0, 0);
    PostThreadMessageA(id, WM_QUIT, 0, 0);
    put("thread", WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
    CloseHandle(thread);
    CloseHandle(ready);
    CloseHandle(out);
}

static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_DESTROY)
        PostQuitMessage(0);
    return DefWindowProcA(window, message, wparam, lparam);
}

void mainCRTStartup(void)
{
    const char *command = GetCommandLineA();
    const char *end = command + lstrlenA(command);
    while (end > command && end[-1] == ' ')
        --end;
    const char number = end > command ? end[-1] : '?';
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = procedure;
    wc.hInstance = GetModuleHandleA(0);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "RunTwice";
    RegisterClassA(&wc);
    char title[32];
    wsprintfA(title, "Run twice %c", number);
    HWND window = CreateWindowA("RunTwice", title, WS_OVERLAPPEDWINDOW, 100 + (number - '1') * 60,
                                100 + (number - '1') * 60, 320, 160, 0, 0, wc.hInstance, 0);
    if (number == '1')
        checks(window);
    ShowWindow(window, SW_SHOW);
    MSG msg;
    while (GetMessageA(&msg, 0, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    ExitProcess(0);
}
