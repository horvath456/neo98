/* SPDX-License-Identifier: GPL-2.0-or-later */
/* V-15: USER draws in a program mode too. Win98 USER paints frames, captions
   and the desktop in every display mode, clipped by the visible regions; the
   port once stopped drawing anything of its own while a program had set a
   mode, and a window with a caption in that mode stood there without one.

   The fixture sets 640x480x16, shows an overlapped window with a caption and
   pumps messages for half a second. The screen has to show the face colour
   in the sizing border and the active caption colour in the title bar; the
   codes name the failing check. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API void Sleep(U);
API U GetTickCount(void);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U DestroyWindow(U);
API U GetDC(U);
API U ReleaseDC(U, U);
API U UpdateWindow(U);
API U PeekMessageA(void *, U, U, U, U);
API U DispatchMessageA(const void *);
API U GetSysColor(int);
API U EnumDisplaySettingsA(const char *, U, void *);
API U ChangeDisplaySettingsExA(const char *, void *, U, U, U);
API U GetPixel(U, int, int);

static void check(int ok, U code) { if (!ok) ExitProcess(code); }
static U __stdcall proc(U h, U m, U w, U l) { return DefWindowProcA(h, m, w, l); }
/* 16 bits keep five or six bits a channel; compare with that much slack. */
static int near(U a, U b)
{
    for (int shift = 0; shift < 24; shift += 8) {
        int d = (int)((a >> shift) & 255) - (int)((b >> shift) & 255);
        if (d < -8 || d > 8) return 0;
    }
    return 1;
}

void mainCRTStartup(void)
{
    U cls[10] = {0, (U)proc, 0, 0, 0, 0, 0, 0, 0, (U)"ModeFrame98"};
    unsigned char mode[156] = {0};
    U msg[7];
    U window, screen, border, caption, start;
    check(RegisterClassA(cls) != 0, 10);
    *(unsigned short *)(mode + 36) = 156;
    check(EnumDisplaySettingsA(0, 0xffffffff, mode) != 0, 11);
    *(U *)(mode + 108) = 640; *(U *)(mode + 112) = 480; *(U *)(mode + 104) = 16;
    check(ChangeDisplaySettingsExA(0, mode, 0, 0, 0) == 0, 12);
    /* WS_OVERLAPPEDWINDOW | WS_VISIBLE */
    window = CreateWindowExA(0, "ModeFrame98", "framed", 0x10cf0000u, 100, 100, 400, 300, 0, 0, 0, 0);
    check(window != 0, 13);
    UpdateWindow(window);
    start = GetTickCount();
    while (GetTickCount() - start < 500) {
        while (PeekMessageA(msg, 0, 0, 0, 1)) DispatchMessageA(msg);
        Sleep(20);
    }
    screen = GetDC(0);
    check(screen != 0, 14);
    border = GetPixel(screen, 102, 250);     /* inside the sizing border */
    caption = GetPixel(screen, 104 + 30, 100 + 4 + 8); /* the title bar, right of the icon */
    ReleaseDC(0, screen);
    check(near(border, GetSysColor(15)), 20);     /* COLOR_3DFACE */
    check(near(caption, GetSysColor(2)), 21);     /* COLOR_ACTIVECAPTION */
    DestroyWindow(window);
    check(ChangeDisplaySettingsExA(0, 0, 0, 0, 0) == 0, 15);
    ExitProcess(0);
}
