/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The hot keys of MouseKeys and FilterKeys. At its start both features are
   off with their hot keys active (MKF_HOTKEYACTIVE, FKF_HOTKEYACTIVE) and
   FilterKeys filters nothing (all of its times zero); a window over the
   screen asks both settings every tenth of a second and notes each change,
   "M1" or "M0" for MouseKeys going on or off, "F1" or "F0" for FilterKeys.
   Esc writes the notes into C:\HOTKEYS.TXT and ends the program.
   tools/test_access_hot_keys_host.py */
#include <windows.h>

static char notes[200];
static int mouse_on, filter_on;

static void look(void)
{
    MOUSEKEYS mouse = {sizeof mouse};
    FILTERKEYS filter = {sizeof filter};
    SystemParametersInfoA(SPI_GETMOUSEKEYS, sizeof mouse, &mouse, 0);
    SystemParametersInfoA(SPI_GETFILTERKEYS, sizeof filter, &filter, 0);
    const int m = (mouse.dwFlags & MKF_MOUSEKEYSON) != 0, f = (filter.dwFlags & FKF_FILTERKEYSON) != 0;
    if (m != mouse_on && lstrlenA(notes) < 190)
        wsprintfA(notes + lstrlenA(notes), "M%d ", m);
    if (f != filter_on && lstrlenA(notes) < 190)
        wsprintfA(notes + lstrlenA(notes), "F%d ", f);
    mouse_on = m;
    filter_on = f;
}

static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_TIMER)
        look();
    else if (message == WM_CHAR && wp == 27)
    {
        look();
        DWORD written = 0;
        HANDLE file = CreateFileA("C:\\HOTKEYS.TXT", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        WriteFile(file, notes, lstrlenA(notes), &written, NULL);
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
    MOUSEKEYS mouse = {sizeof mouse};
    SystemParametersInfoA(SPI_GETMOUSEKEYS, sizeof mouse, &mouse, 0);
    mouse.dwFlags = (mouse.dwFlags & ~(MKF_MOUSEKEYSON | MKF_CONFIRMHOTKEY)) | MKF_HOTKEYACTIVE;
    SystemParametersInfoA(SPI_SETMOUSEKEYS, sizeof mouse, &mouse, 0);
    FILTERKEYS filter = {sizeof filter};
    SystemParametersInfoA(SPI_GETFILTERKEYS, sizeof filter, &filter, 0);
    filter.dwFlags = (filter.dwFlags & ~(FKF_FILTERKEYSON | FKF_CONFIRMHOTKEY)) | FKF_HOTKEYACTIVE;
    filter.iWaitMSec = filter.iDelayMSec = filter.iRepeatMSec = filter.iBounceMSec = 0;
    SystemParametersInfoA(SPI_SETFILTERKEYS, sizeof filter, &filter, 0);
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = procedure;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.lpszClassName = "AccessHotKeys";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("AccessHotKeys", "Hot keys", WS_POPUP | WS_VISIBLE, 0, 0, 1024, 768, NULL, NULL,
                              wc.hInstance, NULL);
    SetFocus(hwnd);
    SetTimer(hwnd, 1, 100, NULL);
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    ExitProcess(0);
}
