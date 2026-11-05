/* SPDX-License-Identifier: GPL-2.0-or-later */
/* with DirectDraw: a child takes the screen the way a game does
 * (DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN, SetDisplayMode 640x480x8, a flipping
 * primary surface painted green, ShowCursor(FALSE)) and ends with
 * ExitProcess(-1), releasing nothing. The parent stays with a white window.
 * Under Win98 DDRAW.DLL gives the exclusive level and the mode back when the
 * process goes, so the parent sees 800x600 again, can take the exclusive
 * level itself and its own ShowCursor level is untouched. The parent prints
 * DDEND lines; tools/test_ddraw_process_end.py checks them and the picture. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U LoadLibraryA(const char *);
API U GetProcAddress(U, const char *);
API void OutputDebugStringA(const char *);
API void ExitProcess(U);
API char *GetCommandLineA(void);
API U CreateProcessA(const char *, char *, U, U, U, U, const char *, const char *, U *, U *);
API U WaitForSingleObject(U, U);
API U MsgWaitForMultipleObjects(U, const U *, U, U, U);
API U GetExitCodeProcess(U, U *);
API U GetTickCount(void);
API void Sleep(U);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefWindowProcA(U, U, U, U);
API U ShowWindow(U, int);
API U UpdateWindow(U);
API U SetForegroundWindow(U);
API U GetSystemMetrics(int);
API U LoadCursorA(U, U);
API U SetCursor(U);
API int ShowCursor(int);
API U PeekMessageA(void *, U, U, U, U);
API U DispatchMessageA(const void *);

typedef U(__stdcall *Create)(void *, void **, void *);
typedef U(__stdcall *M1)(void *);
typedef U(__stdcall *M2)(void *, U);
typedef U(__stdcall *M3)(void *, U, U);
typedef U(__stdcall *M4)(void *, U, U, U);
typedef U(__stdcall *M5)(void *, U, U, U, U);

static char line[256];
static void number(const char *label, U value)
{
    static const char digits[] = "0123456789abcdef";
    U at = 0, i = 0;
    while (label[i]) line[at++] = label[i++];
    line[at++] = '=';
    for (i = 0; i < 8; ++i) line[at++] = digits[(value >> (28 - i * 4)) & 15];
    line[at] = 0;
    OutputDebugStringA(line);
}

static U __stdcall procedure(U window, U message, U wp, U lp)
{
    return DefWindowProcA(window, message, wp, lp);
}

static void pump(U milliseconds)
{
    U message[8];
    const U start = GetTickCount();
    while (GetTickCount() - start < milliseconds)
    {
        while (PeekMessageA(message, 0, 0, 0, 1)) DispatchMessageA(message);
        Sleep(5);
    }
}

static U window(const char *name, U brush)
{
    U fields[10] = {0, (U)procedure, 0, 0, 0, 0, 0, brush, 0, (U)name};
    fields[6] = LoadCursorA(0, 32512);
    RegisterClassA(fields);
    const U hwnd = CreateWindowExA(0, name, name, 0x90000000u, 0, 0, 800, 600, 0, 0, 0, 0);
    if (hwnd) { ShowWindow(hwnd, 5); UpdateWindow(hwnd); SetForegroundWindow(hwnd); }
    return hwnd;
}

/* IDirectDraw slots: 2 Release, 6 CreateSurface, 20 SetCooperativeLevel,
 * 21 SetDisplayMode. IDirectDrawSurface: 11 Flip, 12 GetAttachedSurface,
 * 25 Lock, 32 Unlock. */
static void *direct_draw(void)
{
    void *ddraw = 0;
    const U library = LoadLibraryA("DDRAW.DLL");
    if (!library) return 0;
    Create create = (Create)GetProcAddress(library, "DirectDrawCreate");
    if (!create || create(0, &ddraw, 0)) return 0;
    return ddraw;
}

static void child(void)
{
    static U desc[27], back_desc[27], caps[1];
    void *primary = 0, *back = 0;
    const U hwnd = window("DDENDCHILD", 0);
    void *ddraw = direct_draw();
    if (!hwnd || !ddraw) ExitProcess(7801);
    U **vt = *(U ***)ddraw;
    if (((M3)vt[20])(ddraw, hwnd, 0x13)) ExitProcess(7802);
    if (((M4)vt[21])(ddraw, 640, 480, 8)) ExitProcess(7803);
    desc[0] = 0x6c; desc[1] = 0x21; desc[5] = 1; desc[26] = 0x218;
    if (((M4)vt[6])(ddraw, (U)desc, (U)&primary, 0) || !primary) ExitProcess(7804);
    U **sv = *(U ***)primary;
    caps[0] = 4;
    if (((M3)sv[12])(primary, (U)caps, (U)&back) || !back) ExitProcess(7805);
    back_desc[0] = 0x6c;
    if (!((M5)sv[25])(back, 0, (U)back_desc, 1, 0) && back_desc[9])
    {
        unsigned char *bits = (unsigned char *)back_desc[9];
        for (U y = 0; y < 480; ++y)
            for (U x = 0; x < 640; ++x) bits[y * back_desc[4] + x] = 250; /* green in the default palette */
        ((M2)sv[32])(back, (U)bits);
    }
    ((M3)sv[11])(primary, 0, 1);
    ShowCursor(0);
    OutputDebugStringA("DDEND child took the screen");
    pump(300);
    ExitProcess(0xffffffffu);
}

void mainCRTStartup(void)
{
    const char *command = GetCommandLineA();
    if (command[0] == 'C' && command[1] == 'H') child();   /* "CHILD"; the parent runs as C:\DDEND.EXE */
    const U hwnd = window("DDENDPARENT", 6); /* COLOR_WINDOW + 1: white */
    if (!hwnd) ExitProcess(7701);
    SetCursor(LoadCursorA(0, 32512));
    pump(200);
    U si[17] = {68}, pi[4] = {0};
    char line_child[] = "CHILD";
    if (!CreateProcessA("C:\\DDEND.EXE", line_child, 0, 0, 0, 0, 0, 0, si, pi)) ExitProcess(7702);
    const U start = GetTickCount();
    U message[8];
    while (WaitForSingleObject(pi[0], 0) != 0)
    {
        if (GetTickCount() - start > 30000) ExitProcess(7703);
        MsgWaitForMultipleObjects(1, pi, 0, 100, 0xff);
        while (PeekMessageA(message, 0, 0, 0, 1)) DispatchMessageA(message);
    }
    U code = 0;
    GetExitCodeProcess(pi[0], &code);
    number("DDEND child_code", code);
    pump(300);
    number("DDEND width", GetSystemMetrics(0));
    number("DDEND height", GetSystemMetrics(1));
    /* The exclusive level is free again: the parent can take it. */
    void *ddraw = direct_draw();
    number("DDEND ddraw", ddraw ? 1 : 0);
    if (ddraw)
    {
        U **vt = *(U ***)ddraw;
        number("DDEND exclusive", ((M3)vt[20])(ddraw, hwnd, 0x13));
        number("DDEND normal", ((M3)vt[20])(ddraw, hwnd, 8));
        ((M1)vt[2])(ddraw);
    }
    /* The child's ShowCursor(FALSE) went with its queue. */
    number("DDEND cursor_hide", (U)ShowCursor(0));
    number("DDEND cursor_show", (U)ShowCursor(1));
    SetForegroundWindow(hwnd);
    pump(500);
    OutputDebugStringA("DDEND done");
    /* Stay until the runner's time limit takes the screenshot. */
    for (;;) pump(1000);
}
