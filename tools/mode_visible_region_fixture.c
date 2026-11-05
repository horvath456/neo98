/* SPDX-License-Identifier: GPL-2.0-or-later */
/* in a program mode only the visible region of a window
   reaches the screen. Win98 gives every window DC the visible region as its
   clipping: the window minus the windows in front of it. A full-screen game
   in exclusive DirectDraw mode lies in front of everything, so what Explorer
   and the other programs paint underneath never shows. The port copied the
   whole surface of a window to the screen after every drawing, and
   the desktop flickered through the A5 splash of Kroko-Mania.

   The fixture sets 640x480x16, lays a topmost popup over the upper half and
   fills it black, then fills the whole of a window below it red. The screen
   has to show black above and red below; the codes name the failing check. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U DestroyWindow(U);
API U GetDC(U);
API U ReleaseDC(U, U);
API U FillRect(U, const void *, U);
API U GetStockObject(U);
API U CreateSolidBrush(U);
API U DeleteObject(U);
API U GetPixel(U, int, int);
API U EnumDisplaySettingsA(const char *, U, void *);
API U ChangeDisplaySettingsExA(const char *, void *, U, U, U);

static void check(int ok, U code) { if (!ok) ExitProcess(code); }
static U __stdcall proc(U h, U m, U w, U l) { return DefWindowProcA(h, m, w, l); }
static int red(U c) { return (c & 0xff) >= 0xf0 && (c & 0xffff00) == 0; }
static void fill(U window, U brush, int width, int height)
{
    int rect[4] = {0, 0, width, height};
    U dc = GetDC(window);
    check(dc != 0, 30);
    FillRect(dc, rect, brush);
    ReleaseDC(window, dc);
}

void mainCRTStartup(void)
{
    U cls[10] = {0, (U)proc, 0, 0, 0, 0, 0, 0, 0, (U)"ModeVisibleRegion98"};
    unsigned char mode[156] = {0};
    U below, cover, screen, brush, top, bottom, beside;
    check(RegisterClassA(cls) != 0, 10);
    *(unsigned short *)(mode + 36) = 156;
    check(EnumDisplaySettingsA(0, 0xffffffff, mode) != 0, 11);
    *(U *)(mode + 108) = 640; *(U *)(mode + 112) = 480; *(U *)(mode + 104) = 16;
    check(ChangeDisplaySettingsExA(0, mode, 0, 0, 0) == 0, 12);
    /* WS_POPUP | WS_VISIBLE; the cover is WS_EX_TOPMOST and comes second,
       so it lies in front. The lower window leaves 40 pixels on the right. */
    below = CreateWindowExA(0, "ModeVisibleRegion98", "below", 0x90000000u, 0, 0, 600, 480, 0, 0, 0, 0);
    cover = CreateWindowExA(8, "ModeVisibleRegion98", "cover", 0x90000000u, 0, 0, 640, 240, 0, 0, 0, 0);
    check(below && cover, 13);
    fill(cover, GetStockObject(4), 640, 240);             /* BLACK_BRUSH */
    brush = CreateSolidBrush(0x0000ff);
    fill(below, brush, 600, 480);
    DeleteObject(brush);
    screen = GetDC(0);
    check(screen != 0, 14);
    top = GetPixel(screen, 100, 100);
    bottom = GetPixel(screen, 100, 400);
    beside = GetPixel(screen, 620, 400);
    ReleaseDC(0, screen);
    check(!red(top) && (top & 0xffffff) == 0, 20);      /* covered: stays black */
    check(red(bottom), 21);                              /* visible: red */
    check(!red(beside), 22);                             /* outside the window */
    DestroyWindow(cover);
    DestroyWindow(below);
    check(ChangeDisplaySettingsExA(0, 0, 0, 0, 0) == 0, 15);
    ExitProcess(0);
}
