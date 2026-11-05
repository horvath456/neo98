/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
// A DirectDraw program in full screen, for the seamless view
// (tools/test_seamless_program_mode_host.py).
//
// One popup window over the whole screen, black, as a game has it. Keys:
// E DirectDrawCreate, SetCooperativeLevel(EXCLUSIVE | FULLSCREEN),
// SetDisplayMode(640, 480, 16) -> "exclusive"
// R RestoreDisplayMode -> "restored"
// N SetCooperativeLevel(NORMAL), Release -> "normal"
// S the window shrinks to 300 by 200 at 100,100 -> "small"
// X ExitProcess(0), with whatever is still set
// K a read from address zero: the program crashes with whatever is set
// Every 300 ms the window takes the foreground if it does not have it: the
// keys of the test reach it whatever else the shell shows in the meantime.
typedef unsigned U;
typedef struct { U hwnd, message, wp, lp, time; int x, y; } MSG;
typedef struct { U style, proc, extra, classExtra, instance, icon, cursor, brush;
                 const char *menu, *name; } CLASS;
#define API __declspec(dllimport) __stdcall
API U RegisterClassA(CLASS *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U GetMessageA(MSG *, U, U, U);
API U DispatchMessageA(MSG *);
API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API U SetWindowTextA(U, const char *);
API int GetSystemMetrics(int);
API U MoveWindow(U, int, int, int, int, U);
API U GetStockObject(int);
API U SetTimer(U, U, U, U);
API U GetForegroundWindow(void);
API U SetForegroundWindow(U);
API U DirectDrawCreate(void *, void **, void *);

typedef U (__stdcall *Method1)(void *);
typedef U (__stdcall *Method3)(void *, U, U);
typedef U (__stdcall *Method4)(void *, U, U, U);
static void **draw;
#define CALL(kind, index) ((kind)(*(void ***)draw)[index])

static U __stdcall proc(U h, U m, U wp, U lp)
{
    if (m == 0x113 && GetForegroundWindow() != h) SetForegroundWindow(h); // WM_TIMER
    if (m == 0x100) // WM_KEYDOWN
    {
        if (wp == 'E' && !draw)
        {
            void *object = 0;
            if (DirectDrawCreate(0, &object, 0) || !object) ExitProcess(4);
            draw = (void **)object;
            if (CALL(Method3, 20)(draw, h, 0x11)) ExitProcess(5);          // SetCooperativeLevel
            if (CALL(Method4, 21)(draw, 640, 480, 16)) ExitProcess(6);     // SetDisplayMode
            SetWindowTextA(h, "exclusive");
        }
        if (wp == 'R' && draw) { CALL(Method1, 19)(draw); SetWindowTextA(h, "restored"); }
        if (wp == 'N' && draw)
        {
            CALL(Method3, 20)(draw, h, 8);
            CALL(Method1, 2)(draw);
            draw = 0;
            SetWindowTextA(h, "normal");
        }
        if (wp == 'S') { MoveWindow(h, 100, 100, 300, 200, 1); SetWindowTextA(h, "small"); }
        if (wp == 'X') ExitProcess(0);
        if (wp == 'K') return *(volatile U *)0;
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
    c.brush = GetStockObject(4); // BLACK_BRUSH
    c.name = "NEO98DirectDrawMode";
    if (!RegisterClassA(&c)) ExitProcess(2);
    window = CreateWindowExA(8, c.name, "ready", 0x90000000u, 0, 0, GetSystemMetrics(0), GetSystemMetrics(1), 0, 0,
                             c.instance, 0);
    if (!window) ExitProcess(3);
    SetTimer(window, 1, 300, 0);
    while (GetMessageA(&msg, 0, 0, 0)) DispatchMessageA(&msg);
    ExitProcess(0);
}
