/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest for a DIB pattern brush over 8 by 8 pixels tiles the whole
   DIB for a raster operation that uses the pattern and not the source (GDI sets bit 20h of DC+0Fh, the blit fills ). The
   brush is 16 by 16 pixels whose colour names their own position; PatBlt
   with PATCOPY into a 32-bit DIB section has to show pixel (12,3) of the
   brush at (12,3), not the 8 by 8 corner again, and SetBrushOrgEx moves the
   tile. Exit code zero means every check held; codes 4800..4819. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U CreateDIBPatternBrushPt(const void *, U);
API U CreateDIBSection(U, const void *, U, void **, U, U);
API U CreateCompatibleDC(U);
API U SelectObject(U, U);
API U PatBlt(U, int, int, int, int, U);
API U GdiFlush(void);
API U SetBrushOrgEx(U, int, int, void *);
API U DeleteObject(U);
API U DeleteDC(U);

static U failure;
static void check(int ok, U code)
{
    if (!ok && !failure)
        failure = code;
}
/* The brush pixel at (x, y): blue x*16, green y*16, red 80h (a DIB pixel is
   BGRX in memory, 00RRGGBB as a dword). */
static U tile(U x, U y) { return 0x800000u | (y * 16) << 8 | x * 16; }
void mainCRTStartup(void)
{
    static U packed[10 + 16 * 16];
    packed[0] = 40;                 /* biSize */
    packed[1] = 16;                 /* biWidth */
    packed[2] = 16;                 /* biHeight, bottom-up */
    packed[3] = 1 | 32 << 16;       /* biPlanes, biBitCount */
    for (U y = 0; y < 16; ++y)
        for (U x = 0; x < 16; ++x)
            packed[10 + (15 - y) * 16 + x] = tile(x, y);
    const U brush = CreateDIBPatternBrushPt(packed, 0);
    check(brush != 0, 4800);
    U info[10] = {40, 32, (U) -32, 1 | 32 << 16};
    U *bits = 0;
    const U section = CreateDIBSection(0, info, 0, (void **) &bits, 0, 0);
    check(section && bits, 4801);
    const U dc = CreateCompatibleDC(0);
    check(dc != 0, 4802);
    if (!section || !bits || !dc || !brush)
        ExitProcess(failure);
    SelectObject(dc, section);
    SelectObject(dc, brush);
    check(PatBlt(dc, 0, 0, 32, 32, 0x00f00021) != 0, 4803);
    GdiFlush();
    check((bits[3 * 32 + 12] & 0xffffff) == tile(12, 3), 4804);
    check((bits[3 * 32 + 4] & 0xffffff) == tile(4, 3), 4805);
    check((bits[19 * 32 + 20] & 0xffffff) == tile(4, 3), 4806);
    check((bits[14 * 32 + 30] & 0xffffff) == tile(14, 14), 4807);
    /* The origin moves the tile: surface (x, y) shows tile (x - 5, y - 2). */
    SetBrushOrgEx(dc, 5, 2, 0);
    check(PatBlt(dc, 0, 0, 32, 32, 0x00f00021) != 0, 4808);
    GdiFlush();
    check((bits[3 * 32 + 12] & 0xffffff) == tile(7, 1), 4809);
    check((bits[0 * 32 + 0] & 0xffffff) == tile(11, 14), 4810);
    DeleteDC(dc);
    DeleteObject(section);
    DeleteObject(brush);
    ExitProcess(failure);
}
