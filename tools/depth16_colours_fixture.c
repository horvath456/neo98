/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Colours of GDI drawing after a switch of the desktop to 16 bits per pixel.

   The program switches the screen to 800x600 with 16 bits (5-5-5) with
   ChangeDisplaySettings and opens a popup window at 0,0 of 320 by 360
   pixels. Each of its nine rows of 40 pixels shows the same eight colours,
   drawn a different way:

     0  FillRect straight into the window
     1  FillRect into a compatible bitmap, BitBlt to the window
     2  FillRect into a 16-bit DIB section (BI_RGB, 5-5-5), BitBlt
     3  FillRect into a 16-bit DIB section (BI_BITFIELDS, 5-6-5), BitBlt
     4  FillRect into a 24-bit DIB section, BitBlt
     5  SetDIBitsToDevice of 24-bit bits
     6  StretchDIBits of 16-bit 5-6-5 bits the program wrote itself
     7  StretchDIBits of 16-bit 5-5-5 bits (BI_RGB) the program wrote itself
     8  StretchBlt of the 5-6-5 DIB section of row 3, twice its height

   The other way, a compatible bitmap copied into a 5-6-5 DIB section holds
   the 5-6-5 words of the colours.

   GetPixel of the window at the middle of every square and the colours of
   the words go to C:\DEPTH16.BIN (rows of number, got, wanted) behind the
   count of failed checks; a colour passes within 12 of each component, as
   the brush and the blits of DIBENG round differently. The test reads the
   screen as well. The window stays for eight seconds, then the screen goes
   back to the registry's mode. */
#include <windows.h>
#define SIZE 40
static const COLORREF colours[8] = {
    RGB(0, 128, 128), RGB(255, 0, 0), RGB(0, 255, 0), RGB(0, 0, 255),
    RGB(128, 64, 32), RGB(255, 255, 255), RGB(192, 192, 192), RGB(255, 128, 0)};
static DWORD rows[512], used, failed;
static int close_to(int a, int b) { return a - b <= 12 && b - a <= 12; }
static void check(DWORD got, DWORD want)
{
    rows[used] = used / 3 + 1;
    rows[used + 1] = got; rows[used + 2] = want; used += 3;
    if (!close_to(GetRValue(got), GetRValue(want)) || !close_to(GetGValue(got), GetGValue(want)) ||
        !close_to(GetBValue(got), GetBValue(want)) || (got >> 24) != (want >> 24))
        ++failed;
}
static DWORD from565(WORD v)
{
    return RGB((v >> 11) << 3, ((v >> 5) & 63) << 2, (v & 31) << 3);
}
static void squares(HDC dc, int y)
{
    for (int i = 0; i < 8; ++i)
    {
        RECT r = {i * SIZE, y, i * SIZE + SIZE, y + SIZE};
        HBRUSH brush = CreateSolidBrush(colours[i]);
        FillRect(dc, &r, brush);
        DeleteObject(brush);
    }
}
static HBITMAP make_section(HDC window, int bits, BOOL fields, void **pixels)
{
    struct { BITMAPINFOHEADER h; DWORD masks[3]; } info = {{0}};
    info.h.biSize = sizeof info.h; info.h.biWidth = 8 * SIZE; info.h.biHeight = -SIZE;
    info.h.biPlanes = 1; info.h.biBitCount = bits; info.h.biCompression = fields ? BI_BITFIELDS : BI_RGB;
    info.masks[0] = 0xf800; info.masks[1] = 0x07e0; info.masks[2] = 0x001f;
    return CreateDIBSection(window, (BITMAPINFO *)&info, DIB_RGB_COLORS, pixels, 0, 0);
}
static void section(HDC window, HDC memory, int row, int bits, BOOL fields)
{
    void *pixels = 0;
    HBITMAP dib = make_section(window, bits, fields, &pixels);
    HGDIOBJ old = SelectObject(memory, dib);
    squares(memory, 0);
    BitBlt(window, 0, row * SIZE, 8 * SIZE, SIZE, memory, 0, 0, SRCCOPY);
    if (row == 3)
        StretchBlt(window, 0, 8 * SIZE, 8 * SIZE, SIZE, memory, 0, 0, 8 * SIZE, SIZE / 2, SRCCOPY);
    SelectObject(memory, old);
    DeleteObject(dib);
}
static void raw16(HDC window, int row, BOOL six)
{
    static WORD pixels[SIZE][8 * SIZE];
    for (int y = 0; y < SIZE; ++y)
        for (int x = 0; x < 8 * SIZE; ++x)
        {
            const COLORREF c = colours[x / SIZE];
            const unsigned r = GetRValue(c) >> 3, b = GetBValue(c) >> 3;
            pixels[y][x] = six ? (WORD)(r << 11 | (GetGValue(c) >> 2) << 5 | b)
                               : (WORD)(r << 10 | (GetGValue(c) >> 3) << 5 | b);
        }
    struct { BITMAPINFOHEADER h; DWORD masks[3]; } info = {{0}};
    info.h.biSize = sizeof info.h; info.h.biWidth = 8 * SIZE; info.h.biHeight = -SIZE;
    info.h.biPlanes = 1; info.h.biBitCount = 16; info.h.biCompression = six ? BI_BITFIELDS : BI_RGB;
    info.masks[0] = 0xf800; info.masks[1] = 0x07e0; info.masks[2] = 0x001f;
    StretchDIBits(window, 0, row * SIZE, 8 * SIZE, SIZE, 0, 0, 8 * SIZE, SIZE, pixels, (BITMAPINFO *)&info,
                  DIB_RGB_COLORS, SRCCOPY);
}
static void paint(HWND hwnd)
{
    HDC window = GetDC(hwnd), memory = CreateCompatibleDC(window);
    squares(window, 0);
    HBITMAP compatible = CreateCompatibleBitmap(window, 8 * SIZE, SIZE);
    HGDIOBJ old = SelectObject(memory, compatible);
    squares(memory, 0);
    BitBlt(window, 0, SIZE, 8 * SIZE, SIZE, memory, 0, 0, SRCCOPY);
    /* The other way: the compatible bitmap into a 5-6-5 section. */
    HDC other = CreateCompatibleDC(window);
    WORD *words = 0;
    HBITMAP target = make_section(window, 16, TRUE, (void **)&words);
    HGDIOBJ other_old = SelectObject(other, target);
    BitBlt(other, 0, 0, 8 * SIZE, SIZE, memory, 0, 0, SRCCOPY);
    GdiFlush();
    for (int i = 0; i < 8; ++i)
        check(words ? from565(words[SIZE / 2 * 8 * SIZE + i * SIZE + SIZE / 2]) : 0xffffffff, colours[i]);
    SelectObject(other, other_old);
    DeleteObject(target);
    DeleteDC(other);
    SelectObject(memory, old);
    DeleteObject(compatible);
    section(window, memory, 2, 16, FALSE);
    section(window, memory, 3, 16, TRUE);
    section(window, memory, 4, 24, FALSE);
    static BYTE rgb[SIZE][8 * SIZE * 3];
    for (int y = 0; y < SIZE; ++y)
        for (int x = 0; x < 8 * SIZE; ++x)
        {
            const COLORREF c = colours[x / SIZE];
            rgb[y][x * 3] = GetBValue(c); rgb[y][x * 3 + 1] = GetGValue(c); rgb[y][x * 3 + 2] = GetRValue(c);
        }
    BITMAPINFOHEADER h = {0};
    h.biSize = sizeof h; h.biWidth = 8 * SIZE; h.biHeight = -SIZE; h.biPlanes = 1; h.biBitCount = 24;
    SetDIBitsToDevice(window, 0, 5 * SIZE, 8 * SIZE, SIZE, 0, 0, 0, SIZE, rgb, (BITMAPINFO *)&h, DIB_RGB_COLORS);
    raw16(window, 6, TRUE);
    raw16(window, 7, FALSE);
    GdiFlush();
    for (int row = 0; row < 9; ++row)
        for (int i = 0; i < 8; ++i)
            check(GetPixel(window, i * SIZE + SIZE / 2, row * SIZE + SIZE / 2), colours[i]);
    DeleteDC(memory);
    ReleaseDC(hwnd, window);
}
static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM w, LPARAM l)
{
    if (message == WM_TIMER) { DestroyWindow(hwnd); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    if (message == WM_PAINT)
    {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcA(hwnd, message, w, l);
}
void mainCRTStartup(void)
{
    DEVMODEA mode = {0};
    mode.dmSize = sizeof mode; mode.dmBitsPerPel = 16; mode.dmPelsWidth = 800; mode.dmPelsHeight = 600;
    mode.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
    if (ChangeDisplaySettingsA(&mode, 0) != DISP_CHANGE_SUCCESSFUL) ++failed;
    HDC screen = GetDC(0);
    if (GetDeviceCaps(screen, BITSPIXEL) != 16) ++failed;
    ReleaseDC(0, screen);
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = procedure; wc.hInstance = GetModuleHandleA(0); wc.lpszClassName = "Depth16Colours";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(WS_EX_TOPMOST, wc.lpszClassName, "", WS_POPUP | WS_VISIBLE, 0, 0, 8 * SIZE,
                                9 * SIZE, 0, 0, wc.hInstance, 0);
    UpdateWindow(hwnd);
    paint(hwnd);
    DWORD out[1 + 512], written = 0;
    out[0] = failed;
    for (DWORD i = 0; i < used; ++i) out[1 + i] = rows[i];
    HANDLE file = CreateFileA("C:\\DEPTH16.BIN", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    WriteFile(file, out, (1 + used) * 4, &written, 0);
    CloseHandle(file);
    SetTimer(hwnd, 1, 8000, 0);
    MSG msg;
    while (GetMessageA(&msg, 0, 0, 0) > 0) DispatchMessageA(&msg);
    ChangeDisplaySettingsA(0, 0);
    ExitProcess(failed);
}
