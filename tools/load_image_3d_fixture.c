/* SPDX-License-Identifier: GPL-2.0-or-later */
/* LoadImage of a 16-colour bitmap with LR_LOADTRANSPARENT and
   LR_LOADMAP3DCOLORS (System Properties loads an OEMLOGO.BMP so).

   The bitmap is 4 by 2 pixels from C:\LOADIMG.BMP. Its first pixel, the
   lower left one of the bottom-up rows, has a magenta entry that the
   picture uses once more; the others are grey 808080h, grey C0C0C0h and red.
   LR_LOADTRANSPARENT alone gives the entry of the first pixel COLOR_WINDOW;
   with LR_LOADMAP3DCOLORS it becomes COLOR_3DFACE, and the greys become
   COLOR_3DSHADOW and COLOR_3DFACE. Without either flag the colours stay.
   The rows (number, got, wanted) go to C:\LOADIMG.BIN behind the count of
   failed checks. */
#include <windows.h>
static DWORD rows[96], used, failed;
static void check(DWORD got, DWORD want)
{
    rows[used] = used / 3 + 1;
    rows[used + 1] = got; rows[used + 2] = want; used += 3;
    if (got != want) ++failed;
}
static void write(const char *path, const void *data, DWORD size)
{
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    DWORD written = 0;
    WriteFile(file, data, size, &written, 0);
    CloseHandle(file);
}
/* The eight pixels of the picture, left to right, lower row first. */
static void pixels(HDC dc, HBITMAP bitmap, DWORD out[8])
{
    HGDIOBJ old = SelectObject(dc, bitmap);
    for (int i = 0; i < 8; ++i) out[i] = GetPixel(dc, i % 4, 1 - i / 4);
    SelectObject(dc, old);
}
void mainCRTStartup(void)
{
    /* BITMAPFILEHEADER, BITMAPINFOHEADER, 16 entries, two rows of 4 bytes. */
    static unsigned char file[14 + 40 + 64 + 8];
    BITMAPFILEHEADER *head = (BITMAPFILEHEADER *)file;
    BITMAPINFOHEADER *info = (BITMAPINFOHEADER *)(file + 14);
    RGBQUAD *colours = (RGBQUAD *)(file + 54);
    head->bfType = 0x4d42; head->bfSize = sizeof file; head->bfOffBits = 14 + 40 + 64;
    info->biSize = 40; info->biWidth = 4; info->biHeight = 2; info->biPlanes = 1;
    info->biBitCount = 4; info->biClrUsed = 16;
    const DWORD table[5] = {RGB(0, 0, 0), RGB(255, 0, 255), RGB(128, 128, 128), RGB(192, 192, 192), RGB(255, 0, 0)};
    for (int i = 0; i < 5; ++i)
    {
        colours[i].rgbRed = GetRValue(table[i]);
        colours[i].rgbGreen = GetGValue(table[i]);
        colours[i].rgbBlue = GetBValue(table[i]);
    }
    /* Lower row: magenta, grey 80, grey C0, red; upper row: red, magenta, black, black. */
    const unsigned char bits[8] = {0x12, 0x34, 0, 0, 0x41, 0x00, 0, 0};
    for (int i = 0; i < 8; ++i) file[118 + i] = bits[i];
    write("C:\\LOADIMG.BMP", file, sizeof file);

    HDC screen = GetDC(0), dc = CreateCompatibleDC(screen);
    const DWORD window = GetSysColor(COLOR_WINDOW), face = GetSysColor(COLOR_3DFACE),
                shadow = GetSysColor(COLOR_3DSHADOW);
    DWORD got[8];
    HBITMAP plain = LoadImageA(0, "C:\\LOADIMG.BMP", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
    check(plain != 0, 1);
    pixels(dc, plain, got);
    check(got[0], RGB(255, 0, 255)); check(got[1], RGB(128, 128, 128));
    check(got[2], RGB(192, 192, 192)); check(got[3], RGB(255, 0, 0));
    HBITMAP transparent = LoadImageA(0, "C:\\LOADIMG.BMP", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_LOADTRANSPARENT);
    check(transparent != 0, 1);
    pixels(dc, transparent, got);
    check(got[0], window); check(got[5], window);
    check(got[1], RGB(128, 128, 128)); check(got[3], RGB(255, 0, 0));
    HBITMAP both = LoadImageA(0, "C:\\LOADIMG.BMP", IMAGE_BITMAP, 0, 0,
                              LR_LOADFROMFILE | LR_LOADTRANSPARENT | LR_LOADMAP3DCOLORS);
    check(both != 0, 1);
    pixels(dc, both, got);
    check(got[0], face); check(got[5], face);
    check(got[1], shadow); check(got[2], face); check(got[3], RGB(255, 0, 0));
    HBITMAP mapped = LoadImageA(0, "C:\\LOADIMG.BMP", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_LOADMAP3DCOLORS);
    check(mapped != 0, 1);
    pixels(dc, mapped, got);
    check(got[0], RGB(255, 0, 255)); check(got[1], shadow); check(got[2], face);
    DeleteDC(dc);
    ReleaseDC(0, screen);

    DWORD out[1 + 96];
    out[0] = failed;
    for (DWORD i = 0; i < used; ++i) out[1 + i] = rows[i];
    write("C:\\LOADIMG.BIN", out, (1 + used) * 4);
    ExitProcess(failed);
}
