/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
// A program that shows its window and draws it seconds later
// (tools/test_seamless_first_paint_host.py).
//
// An overlapped window with a caption at 200,150, 400 by 300, without a class
// brush: until WM_PAINT its client holds what the screen had there. The
// program shows it, loads for 2.5 seconds (Sleep) and only then reads its
// queue. WM_PAINT fills the client red and names the window "slow painted".
typedef unsigned U;
typedef struct { U hwnd, message, wp, lp, time; int x, y; } MSG;
typedef struct { U style, proc, extra, classExtra, instance, icon, cursor, brush;
                 const char *menu, *name; } CLASS;
typedef struct { U dc, erase; int left, top, right, bottom; U restore, incremental;
                 unsigned char reserved[32]; } PAINT;
#define API __declspec(dllimport) __stdcall
API U RegisterClassA(CLASS *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U GetMessageA(MSG *, U, U, U);
API U DispatchMessageA(MSG *);
API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API U SetWindowTextA(U, const char *);
API U ShowWindow(U, int);
API U BeginPaint(U, PAINT *);
API U EndPaint(U, const PAINT *);
API U FillRect(U, const int *, U);
API U GetClientRect(U, int *);
API void Sleep(U);
API U CreateSolidBrush(U);

static U red;
static int painted;

static U __stdcall proc(U h, U m, U wp, U lp)
{
    if (m == 0x14) return 1; // WM_ERASEBKGND: nothing to erase with
    if (m == 0x0f)
    {
        PAINT paint;
        int client[4];
        const U dc = BeginPaint(h, &paint);
        GetClientRect(h, client);
        FillRect(dc, client, red);
        EndPaint(h, &paint);
        if (!painted) { painted = 1; SetWindowTextA(h, "slow painted"); }
        return 0;
    }
    if (m == 2) ExitProcess(0);
    return DefWindowProcA(h, m, wp, lp);
}

void mainCRTStartup(void)
{
    CLASS c = {0};
    MSG msg;
    U window;
    c.proc = (U)proc;
    c.instance = GetModuleHandleA(0);
    c.name = "NEO98SlowPaint";
    red = CreateSolidBrush(0x0000ff);
    if (!RegisterClassA(&c)) ExitProcess(2);
    window = CreateWindowExA(0, c.name, "slow", 0x00cf0000u, 200, 150, 400, 300, 0, 0, c.instance, 0);
    if (!window) ExitProcess(3);
    ShowWindow(window, 5);
    Sleep(2500);
    while (GetMessageA(&msg, 0, 0, 0)) DispatchMessageA(&msg);
    ExitProcess(0);
}
