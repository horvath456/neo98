/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
static DWORD rows[128], used, failed, child_changes;
static void check(DWORD got, DWORD want)
{
    rows[used] = used / 3 + 1;
    rows[used + 1] = got; rows[used + 2] = want; used += 3;
    if (got != want) ++failed;
}
static LRESULT CALLBACK window(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_DISPLAYCHANGE && (GetWindowLongA(h, GWL_STYLE) & WS_CHILD)) ++child_changes;
    return DefWindowProcA(h, m, w, l);
}
void mainCRTStartup(void)
{
    DEVMODEA original = {0}, mode = {0};
    original.dmSize = sizeof(original); mode.dmSize = sizeof(mode);
    check(EnumDisplaySettingsA(0, ENUM_CURRENT_SETTINGS, &original), 1);
    mode.dmBitsPerPel = 32; mode.dmFields = DM_BITSPERPEL;
    check(ChangeDisplaySettingsA(&mode, 0), DISP_CHANGE_SUCCESSFUL);
    HDC screen = GetDC(0), memory = CreateCompatibleDC(screen);
    check(GetDeviceCaps(screen, BITSPIXEL), 32);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, 16, 16);
    HGDIOBJ stock = SelectObject(memory, bitmap);
    check(stock != 0, 1);
    check(SetPixel(memory, 3, 3, RGB(255,0,0)), RGB(255,0,0));
    check(SetPixel(memory, 4, 3, RGB(223,223,223)), RGB(223,223,223));
    check(SelectObject(memory, stock) == bitmap, 1);
    HBITMAP matching_depth = CreateBitmap(16, 16, 1, 32, 0);
    HBITMAP wrong_depth = CreateBitmap(16, 16, 1, 16, 0);
    check(SelectObject(memory, wrong_depth) == 0, 1);
    WNDCLASSA wc = {0}; wc.lpfnWndProc = window;
    wc.hInstance = GetModuleHandleA(0); wc.lpszClassName = "DisplayBitmap";
    RegisterClassA(&wc);
    HWND parent = CreateWindowExA(0, wc.lpszClassName, "", WS_POPUP,
                                 20, 20, 180, 100, 0, 0, wc.hInstance, 0);
    HWND child = CreateWindowExA(0, wc.lpszClassName, "", WS_CHILD,
                                0, 0, 100, 50, parent, 0, wc.hInstance, 0);
    mode.dmBitsPerPel = 8;
    check(ChangeDisplaySettingsA(&mode, 0), DISP_CHANGE_SUCCESSFUL);
    check(GetDeviceCaps(screen, BITSPIXEL), 8);
    BITMAP info = {0}; GetObjectA(bitmap, sizeof(info), &info);
    check(info.bmBitsPixel, 32);
    check(child_changes, 0);
    check(SelectObject(memory, bitmap) != 0, 1);
    check(GetPixel(memory, 3, 3), RGB(255,0,0));
    SelectObject(memory, stock);
    check(GetDeviceCaps(memory, BITSPIXEL), 8);
    HDC fresh = CreateCompatibleDC(screen);
    check(GetDeviceCaps(fresh, BITSPIXEL), 8);
    HGDIOBJ fresh_stock = SelectObject(fresh, bitmap);
    check(fresh_stock != 0, 1);
    check(GetPixel(fresh, 3, 3), RGB(255,0,0));
    check(GetSysColor(COLOR_3DLIGHT), RGB(192,192,192));
    check(GetNearestColor(screen, RGB(223,223,223)), RGB(192,192,192));
    HDC target = CreateCompatibleDC(screen);
    HBITMAP indexed = CreateCompatibleBitmap(screen, 16, 16);
    SelectObject(target, indexed);
    check(!!BitBlt(target, 0, 0, 16, 16, fresh, 0, 0, SRCCOPY), 1);
    check(GetPixel(target, 4, 3), RGB(192,192,192));
    DeleteDC(target); DeleteObject(indexed);
    SelectObject(fresh, fresh_stock);
    check(SelectObject(fresh, matching_depth) != 0, 1);
    SelectObject(fresh, fresh_stock);
    check(SelectObject(fresh, wrong_depth) == 0, 1);
    HBITMAP newly_wrong = CreateBitmap(16, 16, 1, 32, 0);
    check(SelectObject(fresh, newly_wrong) == 0, 1);
    DeleteDC(fresh); DeleteObject(newly_wrong); DeleteObject(matching_depth);
    DestroyWindow(parent); DeleteObject(wrong_depth); DeleteObject(bitmap);
    DeleteDC(memory); ReleaseDC(0, screen);
    ChangeDisplaySettingsA(&original, 0);
    HANDLE file = CreateFileA("C:\\UPDRGN.BIN", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    DWORD written;
    WriteFile(file, &failed, 4, &written, 0);
    WriteFile(file, rows, used * 4, &written, 0);
    CloseHandle(file); ExitProcess(failed);
}
