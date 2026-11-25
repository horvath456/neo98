/* SPDX-License-Identifier: GPL-2.0-or-later */
/* MouseKeys: a window over the screen that counts the presses of its
   buttons. At its start MouseKeys is on for the digits of the keypad
   (MKF_REPLACENUMBERS); Esc writes C:\MOUSEKEY.TXT: the position of the
   pointer, the presses of the left and the right button, and the double
   clicks of the left one. tools/test_mouse_keys_host.py */
#include <windows.h>

static int left, right, doubled;

static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_LBUTTONDOWN) ++left;
    else if (message == WM_RBUTTONDOWN) ++right;
    else if (message == WM_LBUTTONDBLCLK) ++doubled;
    else if (message == WM_CHAR && wp == 27)
    {
        POINT p;
        GetCursorPos(&p);
        char line[200];
        DWORD written = 0;
        wsprintfA(line, "%ld %ld %d %d %d\r\n", p.x, p.y, left, right, doubled);
        HANDLE file = CreateFileA("C:\\MOUSEKEY.TXT", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        WriteFile(file, line, lstrlenA(line), &written, NULL);
        CloseHandle(file);
        DestroyWindow(hwnd);
        return 0;
    }
    else if (message == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, message, wp, lp);
}

void mainCRTStartup(void)
{
    MOUSEKEYS keys = {sizeof keys};
    SystemParametersInfoA(SPI_GETMOUSEKEYS, sizeof keys, &keys, 0);
    keys.dwFlags |= MKF_MOUSEKEYSON | MKF_REPLACENUMBERS;
    SystemParametersInfoA(SPI_SETMOUSEKEYS, sizeof keys, &keys, 0);
    WNDCLASSA wc = {0};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = procedure;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.lpszClassName = "MouseKeysTest";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("MouseKeysTest", "MouseKeys", WS_POPUP | WS_VISIBLE, 0, 0, 1024, 768, NULL, NULL,
                              wc.hInstance, NULL);
    SetFocus(hwnd);
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    ExitProcess(0);
}
