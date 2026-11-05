/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A program that reads the mouse the way the mouse emulation of the original
 * DINPUT does: it takes the distance of every WM_MOUSEMOVE from the centre of
 * the screen as its step and puts the cursor back on the centre afterwards.
 * Every step it read goes into the shared buffer, and
 * tools/test_cursor_relative_native.cpp checks them against the movements it
 * fed in. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U GetModuleHandleA(const char *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U ShowWindow(U, int);
API U SetCursorPos(int, int);
API U PeekMessageA(void *, U, U, U, U);
API U TranslateMessage(const void *);
API U DispatchMessageA(const void *);
API U GetTickCount(void);
API void Sleep(U);
API void *VirtualAlloc(void *, U, U, U);
API void ExitProcess(U);

#define RECORDS 0x30000000u
#define CENTRE_X 400
#define CENTRE_Y 300
static int *records;
static U count;

static U __stdcall window_procedure(U hwnd, U message, U wp, U lp)
{
    if (message == 0x200) /* WM_MOUSEMOVE */
    {
        const int x = (int)(short)(lp & 0xffff), y = (int)(short)((lp >> 16) & 0xffff);
        const int dx = x - CENTRE_X, dy = y - CENTRE_Y;
        /* A step of zero is the move the centring itself produced; the
         * original reads it and reports nothing, so it is not a record. */
        if (count < 64)
        {
            records[1 + count * 2] = dx;
            records[2 + count * 2] = dy;
            records[0] = (int)++count;
        }
        if (dx || dy)
            SetCursorPos(CENTRE_X, CENTRE_Y);
        return 0;
    }
    return DefWindowProcA(hwnd, message, wp, lp);
}

static U klass[10];
static char class_name[] = "NEO98CURSOR";

void run_cursor_relative_test(void)
{
    records = VirtualAlloc((void *)RECORDS, 65536, 0x3000, 4);
    if (records != (int *)RECORDS)
        ExitProcess(7500);
    records[0] = 0;
    klass[1] = (U)(void *)window_procedure;
    klass[4] = GetModuleHandleA(0);
    klass[9] = (U)(void *)class_name; /* lpszClassName is at offset 36 */
    if (!RegisterClassA(klass))
        ExitProcess(7501);
    const U hwnd = CreateWindowExA(0, class_name, class_name, 0x90000000u, 0, 0, 800, 600, 0, 0, klass[4], 0);
    if (!hwnd)
        ExitProcess(7502);
    ShowWindow(hwnd, 5);
    SetCursorPos(CENTRE_X, CENTRE_Y);
    U message[8];
    const U start = GetTickCount();
    while (GetTickCount() - start < 20000 && count < 8)
    {
        while (PeekMessageA(message, 0, 0, 0, 1))
        {
            TranslateMessage(message);
            DispatchMessageA(message);
        }
        Sleep(1);
    }
    ExitProcess(0);
}

void mainCRTStartup(void)
{
    run_cursor_relative_test();
}
