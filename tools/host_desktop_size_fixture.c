/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
// The host window follows the size of the desktop (tools/test_host_desktop_size.py).
//
// First start (the registry has no mode, the desktop is 800x600): the fixture
// stores 1024x768x32 with CDS_UPDATEREGISTRY, as DESK.CPL does for "Screen
// area", and covers the screen with a blue window that has a red block
// beyond the old 800 by 600 edge. Titles tell the test what happened:
// "s1" the 1024x768 desktop stands
// "s1 move X,Y" a mouse move reached the window at X,Y
// click -> 640x480 as a program mode (CDS_FULLSCREEN)
// "s2 640x480" the program mode stands, the window covers it
// click at X,Y -> back to the desktop, then 640x480x32 with
// CDS_UPDATEREGISTRY
// "s3 at X,Y" the new desktop stands; X,Y was the click
// "s3 move X,Y" a mouse move on the 640x480 desktop
// Second start (the registry holds 640x480): "restart 640x480", no change.
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
API U TranslateMessage(MSG *);
API U DispatchMessageA(MSG *);
API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API U BeginPaint(U, PAINT *);
API U EndPaint(U, const PAINT *);
API U FillRect(U, const int *, U);
API U CreateSolidBrush(U);
API U SetWindowTextA(U, const char *);
API int ChangeDisplaySettingsA(void *, U);
API int GetSystemMetrics(int);
API U MoveWindow(U, int, int, int, int, U);
API U InvalidateRect(U, const int *, U);

static U red, blue, stage;
static unsigned char mode[0x94];
static void check(U value, U code) { if (!value) ExitProcess(code); }

static char *number(char *p, int value)
{
    char digits[12];
    int n = 0;
    if (value < 0) { *p++ = '-'; value = -value; }
    do { digits[n++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (n) *p++ = digits[--n];
    return p;
}
static void title(U h, const char *prefix, int x, int y, int pair)
{
    char text[64], *p = text;
    while (*prefix) *p++ = *prefix++;
    p = number(p, x);
    *p++ = (char)(pair ? ',' : 'x');
    p = number(p, y);
    *p = 0;
    SetWindowTextA(h, text);
}
static int change(int width, int height, U flags)
{
    *(U *)(mode + 0x6c) = (U)width;
    *(U *)(mode + 0x70) = (U)height;
    return ChangeDisplaySettingsA(mode, flags);
}

static U __stdcall proc(U h, U m, U wp, U lp)
{
    const int x = (short)(lp & 0xffff), y = (short)(lp >> 16);
    if (m == 0x0f)
    {
        PAINT paint;
        const U dc = BeginPaint(h, &paint);
        FillRect(dc, &paint.left, blue);
        if (stage == 1) { const int block[4] = {850, 650, 1000, 750}; FillRect(dc, block, red); }
        EndPaint(h, &paint);
        return 0;
    }
    if (m == 0x200 && (stage == 1 || stage == 3)) { title(h, stage == 1 ? "s1 move " : "s3 move ", x, y, 1); return 0; }
    if (m == 0x201 && stage == 1)
    {
        // A program mode of another size: the host keeps its canvas.
        check(change(640, 480, 4) == 0, 7902);
        check(GetSystemMetrics(0) == 640, 7903);
        stage = 2;
        MoveWindow(h, 0, 0, 640, 480, 1);
        SetWindowTextA(h, "s2 640x480");
        return 0;
    }
    if (m == 0x201 && stage == 2)
    {
        // Back to the desktop, then a new desktop mode in the registry.
        check(ChangeDisplaySettingsA(0, 0) == 0, 7904);
        check(change(640, 480, 1) == 0, 7905);
        check(GetSystemMetrics(0) == 640 && GetSystemMetrics(1) == 480, 7906);
        stage = 3;
        MoveWindow(h, 0, 0, 640, 480, 1);
        InvalidateRect(h, 0, 1);
        title(h, "s3 at ", x, y, 1);
        return 0;
    }
    if (m == 0x10) { ExitProcess(0); return 0; }
    return DefWindowProcA(h, m, wp, lp);
}

void mainCRTStartup(void)
{
    mode[0x24] = 0x94;
    *(U *)(mode + 0x28) = 0x1c0000;
    *(U *)(mode + 0x68) = 32;
    red = CreateSolidBrush(0x000000ff);
    blue = CreateSolidBrush(0x00ff0000);
    const int restarted = GetSystemMetrics(0) == 640;
    if (!restarted)
    {
        check(change(1024, 768, 1) == 0, 7901);
        check(GetSystemMetrics(0) == 1024 && GetSystemMetrics(1) == 768, 7907);
        stage = 1;
    }
    else stage = 10;
    const U instance = GetModuleHandleA(0);
    CLASS c = {0, (U)proc, 0, 0, instance, 0, 0, 0, 0, "HOST_DESKTOP_SIZE"};
    check(RegisterClassA(&c), 7908);
    // WS_POPUP | WS_VISIBLE over the whole screen.
    const U h = CreateWindowExA(0, "HOST_DESKTOP_SIZE", restarted ? "restart 640x480" : "s1", 0x90000000, 0, 0,
                                GetSystemMetrics(0), GetSystemMetrics(1), 0, 0, instance, 0);
    check(h, 7909);
    MSG m;
    while (GetMessageA(&m, 0, 0, 0)) { TranslateMessage(&m); DispatchMessageA(&m); }
    ExitProcess(0);
}
