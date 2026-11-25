/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Controls under a disabled window, as while a modal dialog is open.

   A window at 100,100 of 400x300 pixels holds a push button, an edit, a
   drop-down list, a list box, a static and a check box, all enabled. Half a
   second after the start the window is disabled and repainted with its
   children; Windows draws each control by its own state, so they still look
   enabled. C:\ENABLED.TXT gets seven ints: IsWindowEnabled of the window,
   then of each control. tools/test_ancestor_disabled_host.py */
#include <windows.h>

static HWND window, controls[6];

static LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_TIMER)
    {
        KillTimer(hwnd, 1);
        EnableWindow(hwnd, FALSE);
        RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        int results[7];
        results[0] = IsWindowEnabled(hwnd);
        for (int i = 0; i < 6; ++i) results[1 + i] = IsWindowEnabled(controls[i]);
        HANDLE file = CreateFileA("C:\\ENABLED.TXT", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        DWORD written = 0;
        WriteFile(file, results, sizeof results, &written, NULL);
        CloseHandle(file);
        return 0;
    }
    if (message == WM_DESTROY) PostQuitMessage(0);
    return DefWindowProcA(hwnd, message, wp, lp);
}

void mainCRTStartup(void)
{
    MSG message;
    HINSTANCE instance = GetModuleHandleA(NULL);
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WindowProcedure;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "AncestorDisabled";
    RegisterClassA(&wc);
    window = CreateWindowA("AncestorDisabled", "Disabled window", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 400, 300,
                           NULL, NULL, instance, NULL);
    controls[0] = CreateWindowA("BUTTON", "Push", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 10, 10, 100, 30, window,
                                (HMENU)1, instance, NULL);
    controls[1] = CreateWindowA("EDIT", "Text", WS_CHILD | WS_VISIBLE | WS_BORDER, 10, 50, 150, 24, window, (HMENU)2,
                                instance, NULL);
    controls[2] = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 10, 90, 150, 200, window,
                                (HMENU)3, instance, NULL);
    SendMessageA(controls[2], CB_ADDSTRING, 0, (LPARAM)"Combo");
    SendMessageA(controls[2], CB_SETCURSEL, 0, 0);
    controls[3] = CreateWindowA("LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_BORDER, 10, 130, 150, 60, window, (HMENU)4,
                                instance, NULL);
    SendMessageA(controls[3], LB_ADDSTRING, 0, (LPARAM)"Item");
    controls[4] = CreateWindowA("STATIC", "Label", WS_CHILD | WS_VISIBLE, 10, 200, 100, 20, window, (HMENU)5,
                                instance, NULL);
    controls[5] = CreateWindowA("BUTTON", "Check", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 200, 10, 100, 20, window,
                                (HMENU)6, instance, NULL);
    SetTimer(window, 1, 500, NULL);
    while (GetMessageA(&message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    ExitProcess(0);
}
