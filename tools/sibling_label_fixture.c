// SPDX-License-Identifier: GPL-2.0-or-later
// The time field of TIMEDATE.CPL: a disabled frame EDIT, created first and so
// in front, with the hour and minute EDITs and a ':' STATIC lying on it. No
// control has WS_CLIPSIBLINGS. USER paints the frame first and the STATIC
// after it, so the ':' shows. The exit code names the first step that failed.
typedef unsigned U;
typedef struct { int x, y; } POINT;
typedef struct { U hwnd, message, wParam, lParam, time; POINT pt; } MSG;
#define API __declspec(dllimport) __stdcall
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API int PeekMessageA(MSG *, U, U, U, U);
API int TranslateMessage(const MSG *);
API U DispatchMessageA(const MSG *);
API U SendMessageA(U, U, U, U);
API int ClientToScreen(U, POINT *);
API U GetDC(U);
API int ReleaseDC(U, U);
API U GetPixel(U, int, int);
API U GetStockObject(int);
API U GetModuleHandleA(const char *);
API U GetTickCount(void);
API void Sleep(U);
API void ExitProcess(U);

static void pump(U milliseconds)
{
    MSG m;
    const U end = GetTickCount() + milliseconds;
    while ((int)(end - GetTickCount()) > 0)
    {
        while (PeekMessageA(&m, 0, 0, 0, 1))
        {
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
        Sleep(10);
    }
}

// Whether the screen shows a black pixel inside the client rectangle.
static int black(U hwnd, int width, int height)
{
    POINT origin = {0, 0};
    if (!ClientToScreen(hwnd, &origin)) return 0;
    const U dc = GetDC(0);
    int found = 0;
    for (int y = 0; y < height && !found; ++y)
        for (int x = 0; x < width && !found; ++x)
            found = (GetPixel(dc, origin.x + x, origin.y + y) & 0xffffff) == 0;
    ReleaseDC(0, dc);
    return found;
}

void mainCRTStartup(void)
{
    const U instance = GetModuleHandleA(0);
    const U top = CreateWindowExA(0, "#32770", "Time", 0x10c80000, 40, 40, 200, 100, 0, 0, instance, 0);
    if (!top) ExitProcess(2);
    const U frame = CreateWindowExA(0x200, "EDIT", "", 0x58810000, 10, 10, 77, 20, top, 100, instance, 0);
    const U hour = CreateWindowExA(0, "EDIT", "10", 0x50030006, 13, 13, 12, 13, top, 101, instance, 0);
    const U colon = CreateWindowExA(0, "STATIC", ":", 0x50020081, 25, 13, 6, 13, top, 102, instance, 0);
    const U minute = CreateWindowExA(0, "EDIT", "15", 0x50030006, 31, 13, 12, 13, top, 103, instance, 0);
    // The same STATIC alone, which paints without the question of order.
    const U alone = CreateWindowExA(0, "STATIC", ":", 0x50020081, 25, 50, 6, 13, top, 104, instance, 0);
    if (!frame || !hour || !colon || !minute || !alone) ExitProcess(3);
    const U font = GetStockObject(17);
    const U all[] = {frame, hour, colon, minute, alone};
    for (U i = 0; i < 5; ++i) SendMessageA(all[i], 0x30, font, 0);
    pump(1500);
    if (!black(alone, 6, 13)) ExitProcess(4);
    if (!black(colon, 6, 13)) ExitProcess(5);
    ExitProcess(0);
}
