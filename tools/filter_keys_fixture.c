/* SPDX-License-Identifier: GPL-2.0-or-later */
/* FilterKeys: a window that keeps the characters it gets. "bounce" turns
   FilterKeys on with BounceKeys of 1.5 s, "slow" with SlowKeys of 1 s; Esc
   writes C:\FILTER.TXT with the characters and ends the program.
   tools/test_filter_keys_host.py */
#include <windows.h>

static char typed[256];
static int count;

static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_CHAR && wp == 27)
    {
        DWORD written = 0;
        typed[count++] = '.';
        HANDLE file = CreateFileA("C:\\FILTER.TXT", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        WriteFile(file, typed, count, &written, NULL);
        CloseHandle(file);
        DestroyWindow(hwnd);
        return 0;
    }
    if (message == WM_CHAR && count < 250)
    {
        typed[count++] = (char)wp;
        return 0;
    }
    if (message == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, message, wp, lp);
}

void mainCRTStartup(void)
{
    const char *command = GetCommandLineA();
    const int slow = lstrcmpA(command + lstrlenA(command) - 4, "slow") == 0;
    FILTERKEYS filter = {sizeof filter};
    SystemParametersInfoA(SPI_GETFILTERKEYS, sizeof filter, &filter, 0);
    filter.dwFlags |= FKF_FILTERKEYSON;
    filter.iWaitMSec = slow ? 1000 : 0;
    filter.iBounceMSec = slow ? 0 : 1500;
    SystemParametersInfoA(SPI_SETFILTERKEYS, sizeof filter, &filter, 0);
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = procedure;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "FilterTest";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("FilterTest", "FilterKeys", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, 300, 200, NULL,
                              NULL, wc.hInstance, NULL);
    SetFocus(hwnd);
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    ExitProcess(0);
}
