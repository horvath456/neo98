/* SPDX-License-Identifier: GPL-2.0-or-later */
/* StickyKeys: a window that keeps the characters it gets. At its start
   StickyKeys is off with the hot key active and three states; Esc writes
   C:\STICKY.TXT: the characters (a control character as ^ and its letter)
   and the flags of SPI_GETSTICKYKEYS at the end.
   tools/test_sticky_keys_host.py */
#include <windows.h>

static char typed[256];
static int count;

static void finish(void)
{
    STICKYKEYS sticky = {sizeof sticky};
    SystemParametersInfoA(SPI_GETSTICKYKEYS, sizeof sticky, &sticky, 0);
    char line[400];
    DWORD written = 0;
    typed[count] = 0;
    wsprintfA(line, "%s\r\n%lx\r\n", typed, sticky.dwFlags);
    HANDLE file = CreateFileA("C:\\STICKY.TXT", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    WriteFile(file, line, lstrlenA(line), &written, NULL);
    CloseHandle(file);
}

static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_CHAR && wp == 27)
    {
        finish();
        DestroyWindow(hwnd);
        return 0;
    }
    if (message == WM_CHAR && count < 250)
    {
        if (wp < 32)
        {
            typed[count++] = '^';
            typed[count++] = (char)('A' + wp - 1);
        }
        else
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
    STICKYKEYS sticky = {sizeof sticky};
    SystemParametersInfoA(SPI_GETSTICKYKEYS, sizeof sticky, &sticky, 0);
    sticky.dwFlags = (sticky.dwFlags & ~SKF_STICKYKEYSON) | SKF_HOTKEYACTIVE | SKF_TRISTATE;
    SystemParametersInfoA(SPI_SETSTICKYKEYS, sizeof sticky, &sticky, 0);
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = procedure;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "StickyTest";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("StickyTest", "StickyKeys", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, 300, 200, NULL,
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
