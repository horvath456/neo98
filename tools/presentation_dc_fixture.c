/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
static DWORD rows[24], used, failed;
static void check(DWORD got, DWORD want)
{
    rows[used] = used / 3 + 1;
    rows[used + 1] = got; rows[used + 2] = want; used += 3;
    if (got != want) ++failed;
}
void mainCRTStartup(void)
{
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(0); wc.lpszClassName = "PresentationDC";
    RegisterClassA(&wc);
    HWND w = CreateWindowExA(0, wc.lpszClassName, "", WS_POPUP | WS_VISIBLE,
                             20, 20, 180, 100, 0, 0, wc.hInstance, 0);
    check(w != 0, 1);
    InvalidateRect(w, 0, TRUE);
    check(!!GetUpdateRect(w, 0, FALSE), 1);
    HDC dc = GetDC(w);
    check(dc != 0, 1);
    RECT area = {0, 0, 180, 100};
    FillRect(dc, &area, GetStockObject(WHITE_BRUSH));
    check(!!GetUpdateRect(w, 0, FALSE), 1);
    ReleaseDC(w, dc);
    check(!!GetUpdateRect(w, 0, FALSE), 1);
    ValidateRect(w, 0);
    check(!!GetUpdateRect(w, 0, FALSE), 0);
    DestroyWindow(w);
    HANDLE file = CreateFileA("C:\\UPDRGN.BIN", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    DWORD written;
    WriteFile(file, &failed, 4, &written, 0);
    WriteFile(file, rows, used * 4, &written, 0);
    CloseHandle(file);
    ExitProcess(failed);
}
