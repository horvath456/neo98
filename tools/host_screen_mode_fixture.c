/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The mode the host offers for full screen (tools/test_host_screen_mode.py).

   The fixture looks for 1470x956x32 among the modes of the display and
   stores it with CDS_UPDATEREGISTRY, as the Settings page of Display
   Properties does. Its title says what it found: "offered N stored R WxH"
   with N 1 when the mode is listed, R the answer of ChangeDisplaySettings
   and the screen size afterwards. Started again on the same registry it
   only says "screen WxH". */
#include <windows.h>

static char title[96];

static LRESULT CALLBACK frame(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
}

void mainCRTStartup(void)
{
    HINSTANCE instance = GetModuleHandleA(0);
    static DEVMODEA mode;
    int offered = 0;
    mode.dmSize = sizeof mode;
    for (DWORD i = 0; EnumDisplaySettingsA(0, i, &mode); ++i)
        if (mode.dmPelsWidth == 1470 && mode.dmPelsHeight == 956 && mode.dmBitsPerPel == 32) offered = 1;
    const int width = GetSystemMetrics(SM_CXSCREEN), height = GetSystemMetrics(SM_CYSCREEN);
    if (width == 1470 && height == 956)
        wsprintfA(title, "screen %dx%d", width, height);
    else
    {
        mode.dmSize = sizeof mode;
        mode.dmPelsWidth = 1470; mode.dmPelsHeight = 956; mode.dmBitsPerPel = 32;
        mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
        const LONG result = ChangeDisplaySettingsA(&mode, CDS_UPDATEREGISTRY);
        wsprintfA(title, "offered %d stored %ld %dx%d", offered, result, GetSystemMetrics(SM_CXSCREEN),
                  GetSystemMetrics(SM_CYSCREEN));
    }
    WNDCLASSA k = {0};
    k.hInstance = instance; k.lpfnWndProc = frame; k.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    k.hCursor = LoadCursorA(0, (LPCSTR)IDC_ARROW); k.lpszClassName = "ScreenMode";
    RegisterClassA(&k);
    CreateWindowA("ScreenMode", title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, 400, 200, 0, 0, instance, 0);
    MSG message;
    while (GetMessageA(&message, 0, 0, 0)) { TranslateMessage(&message); DispatchMessageA(&message); }
    ExitProcess(0);
}
