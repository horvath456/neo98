/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A window without a caption at the top left of the screen that counts the
   M keys and the clicks it gets (tools/test_fullscreen_bars_host.py). Its
   title is "keys N clicks M"; a click in its top rows is a click in the
   strip beside a camera housing. */
#include <windows.h>

static char title[48];
static int keys, clicks;

static void say(HWND h)
{
    wsprintfA(title, "keys %d clicks %d", keys, clicks);
    SetWindowTextA(h, title);
}

static LRESULT CALLBACK frame(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_KEYDOWN && w == 'M') { ++keys; say(h); return 0; }
    if (m == WM_LBUTTONDOWN) { ++clicks; say(h); return 0; }
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
}

void mainCRTStartup(void)
{
    HINSTANCE instance = GetModuleHandleA(0);
    WNDCLASSA k = {0};
    k.hInstance = instance; k.lpfnWndProc = frame; k.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    k.hCursor = LoadCursorA(0, (LPCSTR)IDC_ARROW); k.lpszClassName = "FullScreenBars";
    RegisterClassA(&k);
    HWND h = CreateWindowA("FullScreenBars", "keys 0 clicks 0", WS_POPUP | WS_BORDER | WS_VISIBLE, 0, 0, 400, 200, 0,
                           0, instance, 0);
    SetForegroundWindow(h);
    MSG message;
    while (GetMessageA(&message, 0, 0, 0)) { TranslateMessage(&message); DispatchMessageA(&message); }
    ExitProcess(0);
}
