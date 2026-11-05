/* SPDX-License-Identifier: GPL-2.0-or-later */
/* a tree view scrolls in steps of one row of pixels. Each step is
   ScrollWindowEx(0, dy, SW_INVALIDATE|SW_ERASE) into an update region and
   RedrawWindow(region, RDW_INVALIDATE|RDW_ERASE|RDW_ERASENOW), the way
   COMCTL32 4.72 smooth-scrolls; WM_PAINT comes once at
   the end. Every row the steps expose must be painted then. The window
   erases white and paints blue, so a row that stays white lost its update
   region, and one in neither colour was never touched. Exit code 0 is a
   pass, otherwise the failed check. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U ShowWindow(U, U);
API U UpdateWindow(U);
API U BeginPaint(U, void *);
API U EndPaint(U, const void *);
API int FillRect(U, const int *, U);
API U CreateSolidBrush(U);
API int ScrollWindowEx(U, int, int, const int *, const int *, U, int *, U);
API U RedrawWindow(U, const int *, U, U);
API U CreateRectRgn(int, int, int, int);
API U GetDC(U);
API U ReleaseDC(U, U);
API U GetPixel(U, int, int);
API int GetClipBox(U, int *);

static U white, blue, paints;
static void check(int ok, U code) { if (!ok) ExitProcess(code); }
static U __stdcall proc(U h, U m, U w, U l)
{
    if (m == 0x14) { int box[4]; GetClipBox(w, box); FillRect(w, box, white); return 1; }
    if (m == 0xf) {
        U ps[16];
        U dc = BeginPaint(h, ps);
        FillRect(dc, (const int *)&ps[2], blue);
        EndPaint(h, ps);
        ++paints;
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}
void mainCRTStartup(void)
{
    U cls[10] = {0, (U)proc, 0, 0, 0, 0, 0, 0, 0, (U)"ScrollErase98"};
    check(RegisterClassA(cls) != 0, 10);
    white = CreateSolidBrush(0xffffff);
    blue = CreateSolidBrush(0xff0000);
    U window = CreateWindowExA(0, "ScrollErase98", "scroll", 0x90000000u, 100, 100, 200, 200, 0, 0, 0, 0);
    check(window != 0, 11);
    UpdateWindow(window);
    check(paints == 1, 12);
    const int scroll[4] = {0, 0, 200, 200};
    U region = CreateRectRgn(0, 0, 0, 0);
    int update[4];
    for (int step = 0; step < 96; ++step) {
        ScrollWindowEx(window, 0, 1, scroll, scroll, region, update, 6);
        RedrawWindow(window, 0, region, 0x205);
    }
    for (int step = 0; step < 16; ++step) {
        ScrollWindowEx(window, 0, -1, scroll, scroll, region, update, 6);
        RedrawWindow(window, 0, region, 0x205);
    }
    check(paints == 1, 13);   /* ERASENOW does not paint */
    UpdateWindow(window);
    check(paints == 2, 14);
    U dc = GetDC(window);
    U bad = 0, first = 0, colour = 0;
    for (int y = 0; y < 200; ++y) {
        U pixel = GetPixel(dc, 100, y);
        if (pixel != 0xff0000) { if (!bad) { first = y; colour = pixel; } ++bad; }
    }
    /* 1000 + rows not blue; the first one and its colour are in the high bits. */
    if (bad) ExitProcess(1000 + bad + first * 0x10000 + (colour == 0xffffff ? 0x1000000 : 0));
    ReleaseDC(window, dc);
    ExitProcess(0);
}
