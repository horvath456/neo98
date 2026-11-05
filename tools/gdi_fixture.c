// SPDX-License-Identifier: GPL-2.0-or-later
// Own x86 calls verify the wire ABI, pixels, clipping and process-owned cleanup.
#define API __declspec(dllimport) __stdcall
typedef unsigned U;
typedef struct
{
    int left, top, right, bottom;
} Rect;
typedef struct
{
    int x, y;
} Point;
API void ExitProcess(U);
API void Sleep(U);
API void InitCommonControls(void);
API U GetDC(U);
API U ReleaseDC(U, U);
API U CreateSolidBrush(U);
API U CreateCompatibleDC(U);
API U DeleteDC(U);
API U GetObjectA(U, int, void *);
API U SelectObject(U, U);
API U DeleteObject(U);
API U GetStockObject(U);
API int SaveDC(U);
API U RestoreDC(U, int);
API U SetViewportOrgEx(U, int, int, Point *);
API int IntersectClipRect(U, int, int, int, int);
API int ExcludeClipRect(U, int, int, int, int);
API U SetTextColor(U, U);
API U SetBkColor(U, U);
API int SetBkMode(U, int);
API int SetTextCharacterExtra(U, int);
API U SetTextJustification(U, int, int);
API U GetTextExtentPoint32A(U, const char *, int, Point *);
API U GetCharWidth32A(U, U, U, int *);
API U PatBlt(U, int, int, int, int, U);
API U BitBlt(U, int, int, int, int, U, int, int, U);
API U FillRect(U, const Rect *, U);
API U TextOutA(U, int, int, const char *, int);
API U ExtTextOutA(U, int, int, U, const Rect *, const char *, U, const int *);
API U DrawFrameControl(U, Rect *, U, U);
API U GetPixel(U, int, int);
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(15000 + code);
}
void run_gdi_test(void)
{
    InitCommonControls();
    U dc = GetDC(0);
    check(dc != 0, 1);
    U brush = CreateSolidBrush(0x112233);
    check(brush != 0, 2);
    U info[4] = {99, 99, 99, 0xabcdef};
    check(GetObjectA(brush, 0, 0) == 12, 3);
    check(GetObjectA(brush, 12, info) == 12 && info[0] == 0 && info[1] == 0x112233 && !info[2] &&
              info[3] == 0xabcdef,
          4);
    info[0] = info[1] = info[2] = 99;
    check(GetObjectA(brush, 4, info) == 4 && !info[0] && info[1] == 99 && info[2] == 99 &&
              info[3] == 0xabcdef,
          34);
    check(!GetObjectA(0xdeadbeef, 12, info) && info[1] == 99, 35);
    U previous = SelectObject(dc, brush);
    check(previous != 0 && !DeleteObject(brush), 5);
    Point origin = {99, 99};
    check(SetViewportOrgEx(dc, 40, 50, &origin) && !origin.x && !origin.y, 6);
    check(PatBlt(dc, 0, 0, 40, 30, 0xf00021) && GetPixel(dc, 1, 1) == 0x112233, 7);
    int saved = SaveDC(dc);
    check(saved > 0, 8);
    check(IntersectClipRect(dc, 5, 5, 35, 25) == 2, 9);
    check(ExcludeClipRect(dc, 10, 10, 20, 20) == 3, 10);
    check(PatBlt(dc, 0, 0, 40, 30, 0xff0062) && GetPixel(dc, 6, 6) == 0xffffff &&
              GetPixel(dc, 12, 12) == 0xffffffff,
          11);
    check(RestoreDC(dc, saved) && GetPixel(dc, 12, 12) == 0x112233 && GetPixel(dc, 4, 4) == 0x112233 &&
              GetPixel(dc, 6, 6) == 0xffffff,
          12);
    U red = CreateSolidBrush(0xff);
    Rect r = {0, 0, 5, 5};
    check(red != 0 && FillRect(dc, &r, red), 13);
    check(BitBlt(dc, 2, 0, 5, 5, dc, 0, 0, 0xcc0020) && GetPixel(dc, 6, 2) == 0xff &&
              GetPixel(dc, 7, 2) == 0x112233,
          14);
    r = (Rect){25, 0, 30, 5};
    check(FillRect(dc, &r, 6) && GetPixel(dc, 26, 2) == 0xffffff, 15);
    U font = GetStockObject(16);
    check(font != 0 && SelectObject(dc, font) != 0, 16);
    int widths[4] = {0, 0, 0, 0x12345};
    Point extent;
    check(GetCharWidth32A(dc, 'A', 'C', widths) && widths[0] > 0 && widths[3] == 0x12345, 17);
    check(GetTextExtentPoint32A(dc, "ABC", 3, &extent) && extent.x == widths[0] + widths[1] + widths[2], 18);
    int base = extent.x;
    check(SetTextCharacterExtra(dc, 2) == 0, 19);
    check(GetTextExtentPoint32A(dc, "ABC", 3, &extent) && extent.x == base + 6, 20);
    check(SetTextCharacterExtra(dc, 0) == 2 && SetTextJustification(dc, 6, 2), 21);
    check(SetTextJustification(dc, 0, 0) && SetBkMode(dc, 1) == 2, 22);
    SetBkColor(dc, 0xff);
    SetTextColor(dc, 0xffffff);
    r = (Rect){0, 40, 60, 60};
    int advances[3] = {12, -4, 16};
    check(ExtTextOutA(dc, 1, 42, 6, &r, "A\0B", 3, advances) && GetPixel(dc, 59, 59) == 0xff, 23);
    check(TextOutA(dc, 65, 42, "Win98 GDI", 9), 24);
    unsigned ink = 0;
    for (int y = 42; y < 57; ++y)
        for (int x = 0; x < 30; ++x)
            ink += GetPixel(dc, x, y) == 0xffffff;
    check(ink > 20, 25);
    check(!TextOutA(dc, 0, 0, 0, 1) && !ExtTextOutA(dc, 0, 0, 0, 0, "x", 4097, 0), 26);
    check(!GetCharWidth32A(dc, 2, 1, widths) && widths[3] == 0x12345, 27);
    check(GetPixel(dc, 800, 0) == 0xffffffff && GetPixel(0, 0, 0) == 0xffffffff, 28);
    Rect button = {100, 80, 170, 125};
    check(DrawFrameControl(dc, &button, 4, 0x2410) && button.left == 102 && button.top == 82 &&
              button.right == 168 && button.bottom == 123,
          36);
    check(GetPixel(dc, 110, 90) == 0xc0c0c0 && GetPixel(dc, 111, 90) == 0xffffff, 37);
    Sleep(30); // The host can inspect the same pixels that the x86 app read.
    check(SelectObject(dc, previous) != 0 && DeleteObject(red) && DeleteObject(brush), 29);
    check(ReleaseDC(0, dc), 30);
    for (U pass = 0; pass < 40; ++pass)
    {
        U memory = CreateCompatibleDC(0), color = CreateSolidBrush(pass);
        check(memory != 0 && color != 0 && SelectObject(memory, color) != 0 && SaveDC(memory) > 0, 31);
        check(!DeleteObject(color) && DeleteDC(memory) && DeleteObject(color) && !DeleteDC(memory), 32);
    }
    // These two objects deliberately survive this function. Process teardown
    // must delete the DC first, releasing the brush and its saved references.
    U memory = CreateCompatibleDC(0), color = CreateSolidBrush(0xabcdef);
    check(memory != 0 && color != 0 && SelectObject(memory, color) != 0 && SaveDC(memory) > 0, 33);
}
