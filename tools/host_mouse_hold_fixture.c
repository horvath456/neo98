/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
// A program that holds the mouse (tools/test_mouse_hold_host.py).
//
// One popup window at 100,100, 400 by 300. A timer of 20 ms puts the pointer
// back to the middle of the window (SetCursorPos 300,250), the way a game
// recentres it. The title tells the test what the guest saw:
// "ready" at the start
// "down N" the Nth WM_LBUTTONDOWN
// Keys:
// Z forget the mouse moves seen so far -> "zero"
// M "moved" when a WM_MOUSEMOVE away from the middle came since Z,
// "still" otherwise
// H recentring off and on -> "idle", "holding"
// C recentring off, ClipCursor to the window -> "clipped"
// U ClipCursor(NULL) -> "unclipped"
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
API U SetTimer(U, U, U, U);
API U SetCursorPos(int, int);
API U ClipCursor(const int *);
API U GetStockObject(int);

static int holding = 1, moves, downs;

static U __stdcall proc(U h, U m, U wp, U lp)
{
    const int x = (short)(lp & 0xffff), y = (short)(lp >> 16);
    if (m == 0x113 && holding) SetCursorPos(300, 250); // WM_TIMER
    if (m == 0x200 && (x != 200 || y != 150)) ++moves; // WM_MOUSEMOVE, client coordinates
    if (m == 0x201) // WM_LBUTTONDOWN
    {
        char text[16] = "down ";
        text[5] = (char)('0' + ++downs % 10);
        text[6] = 0;
        SetWindowTextA(h, text);
    }
    if (m == 0x100) // WM_KEYDOWN
    {
        static const int rect[4] = {100, 100, 500, 400};
        if (wp == 'Z') { moves = 0; SetWindowTextA(h, "zero"); }
        if (wp == 'M') SetWindowTextA(h, moves ? "moved" : "still");
        if (wp == 'H') { holding = !holding; SetWindowTextA(h, holding ? "holding" : "idle"); }
        if (wp == 'C') { holding = 0; ClipCursor(rect); SetWindowTextA(h, "clipped"); }
        if (wp == 'U') { ClipCursor(0); SetWindowTextA(h, "unclipped"); }
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
    c.brush = GetStockObject(0); // WHITE_BRUSH
    c.name = "NEO98MouseHold";
    if (!RegisterClassA(&c)) ExitProcess(2);
    window = CreateWindowExA(0, c.name, "ready", 0x90000000u, 100, 100, 400, 300, 0, 0, c.instance, 0);
    if (!window) ExitProcess(3);
    SetTimer(window, 1, 20, 0);
    while (GetMessageA(&msg, 0, 0, 0)) DispatchMessageA(&msg);
    ExitProcess(0);
}
