/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U CreateBitmap(U, U, U, U, const void *);
API U CreateBitmapIndirect(const void *);
API U GetBitmapBits(U, U, void *);
API U CreatePatternBrush(U);
API U GetDC(U);
API U ReleaseDC(U, U);
API U SetTextColor(U, U);
API U SetBkColor(U, U);
API U PatBlt(U, int, int, int, int, U);
API U GetPixel(U, int, int);
API U SetBitmapBits(U, U, const void *);
API U GetObjectA(U, U, void *);
API U CreateCompatibleDC(U);
API U SelectObject(U, U);
API U SaveDC(U);
API U RestoreDC(U, int);
API U DeleteDC(U);
API U DeleteObject(U);
API U BitBlt(U, int, int, int, int, U, int, int, U);
API U GetLastError(void);
API void SetLastError(U);
API void ExitProcess(U);
static unsigned char source[90000], output[90000];
static void check(int value, U code)
{
    if (!value)
        ExitProcess(code);
}
void run_bitmap_test(void)
{
    U info[6];
    for (U i = 0; i < 16; i++)
        source[i] = i & 1 ? 0 : 0xf0;
    SetLastError(0x12345678);
    U b = CreateBitmap(8, 8, 1, 1, source);
    check(b && GetLastError() == 0x12345678, 5200);
    check(GetObjectA(b, 24, info) == 24 && info[0] == 0 && info[1] == 8 && info[2] == 8 && info[3] == 2 &&
              info[4] == 0x10001 && info[5] == 0,
          5201);
    check(GetBitmapBits(b, 100, output) == 16, 5202);
    check(GetObjectA(b, 0, 0) == 84, 5229);
    for (U i = 0; i < 100; i++)
        output[i] = 0xa5;
    check(GetObjectA(b, 100, output) == 84 && output[84] == 0xa5, 5230);
    for (U i = 24; i < 84; i++)
        check(!output[i], 5231);
    check(!GetObjectA(b, 0, output) && GetLastError() == 87, 5232);
    check(GetBitmapBits(b, 100, output) == 16, 5233);
    for (U i = 0; i < 16; i++)
        check(source[i] == output[i], 5203);
    check(!GetBitmapBits(b, 16, 0) && GetLastError() == 87, 5204);
    check(!GetBitmapBits(b, 16, (void *)0xfffffff0) && GetLastError() == 87, 5205);
    U dc = CreateCompatibleDC(0), old = SelectObject(dc, b);
    check(dc && old && SaveDC(dc) == 1, 5206);
    check(DeleteObject(b) && GetBitmapBits(b, 16, output) == 16, 5207);
    check(SelectObject(dc, old) == b && DeleteObject(b) && GetBitmapBits(b, 16, output) == 16, 5208);
    check(RestoreDC(dc, -1) && SelectObject(dc, old) == b && DeleteObject(b), 5209);
    check(!GetBitmapBits(b, 16, output) && DeleteDC(dc), 5210);
    info[0] = 7;
    info[1] = 8;
    info[2] = 8;
    info[3] = 999;
    info[4] = 0x10001;
    info[5] = (U)source;
    b = CreateBitmapIndirect(info);
    check(b && GetObjectA(b, 24, info) == 24 && info[0] == 0 && info[3] == 2, 5211);
    U pattern = CreatePatternBrush(b);
    check(pattern, 5221);
    for (U i = 0; i < 16; i++)
        source[i] = 0;
    check(SetBitmapBits(b, 16, source) == 16 && DeleteObject(b), 5222);
    U screen = GetDC(0), previous = SelectObject(screen, pattern);
    check(screen && previous, 5223);
    U text = SetTextColor(screen, 0x0000ff), background = SetBkColor(screen, 0xff0000);
    check(PatBlt(screen, 0, 0, 16, 16, 0xf00021), 5224);
    for (U y = 0; y < 16; y++)
        for (U x = 0; x < 16; x++)
            check(GetPixel(screen, x, y) == ((x & 7) < 4 ? 0xff0000 : 0x0000ff), 5225);
    SetTextColor(screen, 0x00ff00);
    SetBkColor(screen, 0xffffff);
    check(PatBlt(screen, 0, 0, 16, 16, 0xf00021), 5226);
    for (U y = 0; y < 16; y++)
        for (U x = 0; x < 16; x++)
            check(GetPixel(screen, x, y) == ((x & 7) < 4 ? 0xffffff : 0x00ff00), 5227);
    SetTextColor(screen, text);
    SetBkColor(screen, background);
    check(SelectObject(screen, previous) == pattern && ReleaseDC(0, screen) && DeleteObject(pattern), 5228);
    for (U i = 0; i < sizeof source; i++)
        source[i] = (i * 37 + i / 30) & 255;
    U src = CreateBitmap(240, 3000, 1, 1, source), dst = CreateBitmap(240, 3000, 1, 1, 0);
    U sd = CreateCompatibleDC(0), dd = CreateCompatibleDC(0);
    check(src && dst && sd && dd && SelectObject(sd, src) && SelectObject(dd, dst), 5212);
    check(BitBlt(dd, 0, 0, 240, 3000, sd, 0, 0, 0xcc0020), 5213);
    check(GetBitmapBits(dst, sizeof output, output) == sizeof output, 5214);
    for (U i = 0; i < sizeof source; i++)
        check(source[i] == output[i], 5215);
    for (U i = 0; i < sizeof source; i++)
        source[i] = 0xa5;
    check(SetBitmapBits(src, sizeof source, source) == sizeof source, 5216);
    check(BitBlt(dd, 0, 0, 240, 3000, sd, 0, 0, 0xcc0020), 5217);
    check(GetBitmapBits(dst, sizeof output, output) == sizeof output, 5218);
    for (U i = 0; i < sizeof source; i++)
        check(source[i] == output[i], 5219);
    check(DeleteDC(sd) && DeleteDC(dd) && DeleteObject(src) && DeleteObject(dst), 5220);
}
