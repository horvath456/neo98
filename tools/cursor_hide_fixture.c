/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A program that hides the pointer and ends without showing it again, the way
 * Moorhuhn does. It covers the screen with one window, sets the arrow,
 * calls ShowCursor(FALSE) once and reports the answer in the shared buffer.
 * Then it waits until tools/test_cursor_hide_native.cpp has looked at the
 * pointer and ends through ExitProcess. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U GetModuleHandleA(const char *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U ShowWindow(U, int);
API U LoadCursorA(U, U);
API U SetCursor(U);
API int ShowCursor(int);
API U PeekMessageA(void *, U, U, U, U);
API U DispatchMessageA(const void *);
API U GetTickCount(void);
API void Sleep(U);
API void *VirtualAlloc(void *, U, U, U);
API void ExitProcess(U);

#define RECORDS 0x30000000u
static volatile int *records;
static U klass[10];
static char class_name[] = "NEO98HIDE";

static U __stdcall window_procedure(U hwnd, U message, U wp, U lp)
{
    return DefWindowProcA(hwnd, message, wp, lp);
}

void mainCRTStartup(void)
{
    records = VirtualAlloc((void *)RECORDS, 65536, 0x3000, 4);
    if (records != (int *)RECORDS)
        ExitProcess(7600);
    klass[1] = (U)(void *)window_procedure;
    klass[4] = GetModuleHandleA(0);
    klass[9] = (U)(void *)class_name;
    if (!RegisterClassA(klass))
        ExitProcess(7601);
    const U hwnd = CreateWindowExA(0, class_name, class_name, 0x90000000u, 0, 0, 800, 600, 0, 0, klass[4], 0);
    if (!hwnd)
        ExitProcess(7602);
    ShowWindow(hwnd, 5);
    SetCursor(LoadCursorA(0, 32512));
    records[0] = ShowCursor(0);
    records[1] = 1;
    U message[8];
    const U start = GetTickCount();
    while (!records[2] && GetTickCount() - start < 20000)
    {
        while (PeekMessageA(message, 0, 0, 0, 1))
            DispatchMessageA(message);
        Sleep(1);
    }
    ExitProcess(records[2] ? 0 : 7603);
}
