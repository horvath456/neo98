/* SPDX-License-Identifier: GPL-2.0-or-later
   A program that holds the mouse the three ways games do, for
   tools/test_pause_mouse_host.py.

   One popup window at 100,100, 400 by 300. It starts as a game that puts the
   pointer back to the middle of its window (SetCursorPos 300,250) on a timer
   of 20 ms. Keys change the way:
     C no recentring, ClipCursor to the window -> "clipped"
     D no recentring, no clip, the system mouse of DirectInput
        exclusive and in the foreground, read on the timer -> "dinput" or "dinput failed N"
   and ask for what it saw:
     Z forget everything seen so far -> "zero N"
     W append a line to D:\PMOUSE.TXT -> "wrote N"
   A line holds: the largest step of the pointer in one WM_MOUSEMOVE (against
   the middle while recentring, against the place before otherwise), the
   number of such messages, the sum and the largest single reading of the
   movements DirectInput reported, the presses, where GetCursorPos has the
   pointer, the sum of the Z axis of DirectInput and the sum of the deltas of
   WM_MOUSEWHEEL (tools/test_wheel_apps_host.py). */
typedef unsigned U;
typedef struct { U hwnd, message, wp, lp, time; int x, y; } MSG;
typedef struct { U style, proc, extra, classExtra, instance, icon, cursor, brush;
                 const char *menu, *name; } CLASS;
typedef struct { U a; unsigned short b, c; unsigned char d[8]; } GUID;
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
API U GetCursorPos(int *);
API U ClipCursor(const int *);
API U SetFocus(U);
API U ChangeDisplaySettingsA(void *, U);
API U SetWindowPos(U,U,int,int,int,int,U);
API U GetStockObject(int);
API U CreateFileA(const char *, U, U, void *, U, U, U);
API U SetFilePointer(U, int, int *, U);
API int WriteFile(U, const void *, U, U *, void *);
API int CloseHandle(U);
API U DirectInputCreateA(U, U, void **, void *);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);

static const GUID sys_mouse = {0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID x_axis = {0xA36D02E0, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID y_axis = {0xA36D02E1, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID z_axis = {0xA36D02E2, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
/* c_dfDIMouse, as in tools/dinput_vxd_fixture.c. */
static struct { const GUID *guid; U offset, type, flags; } objects[7] = {
    {&x_axis, 0, 0x00ffff03, 0}, {&y_axis, 4, 0x00ffff03, 0}, {&z_axis, 8, 0x80ffff03, 0},
    {0, 12, 0x00ffff0c, 0}, {0, 13, 0x00ffff0c, 0}, {0, 14, 0x80ffff0c, 0}, {0, 15, 0x80ffff0c, 0}};
static struct { U size, object_size, flags, data_size, count; void *objects; } format = {24, 16, 2, 16, 7, objects};
#define CALL(object, index, ...) (((U(__stdcall *)())((*(void ***)(object))[index]))(object, ##__VA_ARGS__))

static int recentring = 1, have_last, last_x, last_y;
static int centre_x = 300, centre_y = 250, left = 100, top = 100;
static U step, moves, downs, di_sum, di_largest, di_buttons, serial;
static int di_z, wheel;
static void *dinput, *mouse;

static U Abs(int v) { return v < 0 ? (U)-v : (U)v; }
static void Title(U h, const char *word, U number)
{
    char text[32];
    wsprintfA(text, "%s %u", word, number);
    SetWindowTextA(h, text);
}
static U StartDirectInput(U window)
{
    if (DirectInputCreateA(GetModuleHandleA(0), 0x0500, &dinput, 0)) return 1;
    if (CALL(dinput, 3, &sys_mouse, &mouse, 0)) return 2;               /* CreateDevice */
    if (CALL(mouse, 11, &format)) return 3;                               /* SetDataFormat */
    if (CALL(mouse, 13, window, 1 | 4)) return 4;                         /* EXCLUSIVE | FOREGROUND */
    if (CALL(mouse, 7)) return 5;                                         /* Acquire */
    return 0;
}
static U __stdcall proc(U h, U m, U wp, U lp)
{
    const int x = (short)(lp & 0xffff), y = (short)(lp >> 16);
    if (m == 0x113)   /* WM_TIMER */
    {
        if (recentring) SetCursorPos(centre_x, centre_y);
        if (mouse)
        {
            int state[4] = {0};
            if (CALL(mouse, 9, 16, state) == 0)                           /* GetDeviceState */
            {
                const U moved = Abs(state[0]) + Abs(state[1]);
                di_buttons |= (U)state[3];
                di_z += state[2];
                di_sum += moved;
                if (moved > di_largest) di_largest = moved;
            }
            else CALL(mouse, 7);                                          /* lost: Acquire again */
        }
    }
    if (m == 0x200)   /* WM_MOUSEMOVE, client coordinates */
    {
        const int from_x = recentring || !have_last ? centre_x-left : last_x;
        const int from_y = recentring || !have_last ? centre_y-top : last_y;
        const U distance = Abs(x - from_x) > Abs(y - from_y) ? Abs(x - from_x) : Abs(y - from_y);
        if (distance) ++moves;
        if (distance > step) step = distance;
        last_x = x; last_y = y; have_last = 1;
    }
    if (m == 0x201) ++downs;   /* WM_LBUTTONDOWN */
    if (m == 0x20a) { wheel += (short)(wp >> 16); return 0; }   /* WM_MOUSEWHEEL */
    if (m == 0x100)   /* WM_KEYDOWN */
    {
        static const int rect[4] = {100, 100, 500, 400};
        if (wp == '1' || wp == '2')
        {
            U mode[39] = {0};
            const int width = wp == '1' ? 320 : 640, height = wp == '1' ? 240 : 480;
            ((unsigned short *)mode)[18] = sizeof mode;
            mode[10] = 0x1c0000; /* DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT */
            mode[26] = wp == '1' ? 16 : 32; mode[27] = width; mode[28] = height;
            ClipCursor(0);
            if (ChangeDisplaySettingsA(mode, 4)) { SetWindowTextA(h, "mode failed"); return 0; }
            SetWindowPos(h, 0, 0, 0, width, height, 4);
            centre_x = width/2; centre_y = height/2; left = top = 0;
            recentring = 1; have_last = 0;
            SetWindowTextA(h, wp == '1' ? "mode320" : "mode640");
        }
        if (wp == 'Z') { step = moves = downs = di_sum = di_largest = di_buttons = 0; di_z = wheel = 0; Title(h, "zero", ++serial); }
        if (wp == 'C') { recentring = 0; ClipCursor(rect); SetWindowTextA(h, "clipped"); }
        if (wp == 'D')
        {
            U failed;
            recentring = 0;
            ClipCursor(0);
            failed = StartDirectInput(h);
            if (failed) Title(h, "dinput failed", failed);
            else SetWindowTextA(h, "dinput");
        }
        if (wp == 'U' && mouse)
        {
            CALL(mouse, 8); /* Unacquire */
            CALL(mouse, 2); /* Release: the timer must not acquire it again. */
            mouse = 0;
            SetWindowTextA(h, "unacquired");
        }
        if (wp == 'W')
        {
            char text[160];
            int at[2] = {0, 0};
            U written, n, file;
            GetCursorPos(at);
            n = wsprintfA(text, "step=%u moves=%u downs=%u di_sum=%u di_largest=%u x=%d y=%d di_z=%d wheel=%d di_buttons=%u\r\n", step,
                          moves, downs, di_sum, di_largest, at[0], at[1], di_z, wheel, di_buttons);
            file = CreateFileA("D:\\PMOUSE.TXT", 0x40000000, 0, 0, 4, 0x80, 0);   /* OPEN_ALWAYS */
            SetFilePointer(file, 0, 0, 2);
            WriteFile(file, text, n, &written, 0);
            CloseHandle(file);
            Title(h, "wrote", ++serial);
        }
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
    c.brush = GetStockObject(0);   /* WHITE_BRUSH */
    c.name = "NEO98PauseMouse";
    if (!RegisterClassA(&c)) ExitProcess(2);
    window = CreateWindowExA(0, c.name, "ready", 0x90000000u, 100, 100, 400, 300, 0, 0, c.instance, 0);
    if (!window) ExitProcess(3);
    SetFocus(window);
    SetTimer(window, 1, 20, 0);
    while (GetMessageA(&msg, 0, 0, 0)) DispatchMessageA(&msg);
    ExitProcess(0);
}
