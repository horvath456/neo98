/* SPDX-License-Identifier: GPL-2.0-or-later */
/* a program sets 640x480x8 with ChangeDisplaySettings, covers the
 * screen with a white window and shows the arrow, the way Pool 'm Up does
 * after its title. Win98's display driver draws the pointer in every mode,
 * so the pointer is there until the program hides it with ShowCursor(FALSE).
 * tools/test_mode_pointer_native.cpp looks at the published frame.
 *
 * Records at 0x30000000: [1] stage (1 pointer shown, 2 pointer hidden),
 * [2] go from the runner, [3] end. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U GetModuleHandleA(const char *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U ShowWindow(U, int);
API U UpdateWindow(U);
API U LoadCursorA(U, U);
API U SetCursor(U);
API int ShowCursor(int);
API U PeekMessageA(void *, U, U, U, U);
API U DispatchMessageA(const void *);
API U GetStockObject(int);
API U EnumDisplaySettingsA(const char *, U, void *);
API int ChangeDisplaySettingsA(void *, U);
API U GetTickCount(void);
API void Sleep(U);
API void *VirtualAlloc(void *, U, U, U);
API void ExitProcess(U);

#define RECORDS 0x30000000u
static volatile int *records;

static U __stdcall window_procedure(U hwnd, U message, U wp, U lp)
{
    return DefWindowProcA(hwnd, message, wp, lp);
}

static void pump_until(volatile int *flag, U limit)
{
    U message[8];
    const U start = GetTickCount();
    while (!*flag && GetTickCount() - start < limit)
    {
        while (PeekMessageA(message, 0, 0, 0, 1))
            DispatchMessageA(message);
        Sleep(1);
    }
}

void mainCRTStartup(void)
{
    records = VirtualAlloc((void *)RECORDS, 65536, 0x3000, 4);
    if (records != (int *)RECORDS)
        ExitProcess(7600);
    const U arrow = LoadCursorA(0, 32512);
    U klass[10] = {0};
    klass[1] = (U)(void *)window_procedure;
    klass[4] = GetModuleHandleA(0);
    klass[6] = arrow;
    klass[7] = GetStockObject(0); /* WHITE_BRUSH */
    klass[9] = (U)(void *)"NEO98MODEPOINTER";
    if (!RegisterClassA(klass))
        ExitProcess(7601);
    unsigned char mode[156] = {0};
    *(unsigned short *)(mode + 36) = 156;
    if (!EnumDisplaySettingsA(0, 0xffffffff, mode))
        ExitProcess(7602);
    *(U *)(mode + 108) = 640; *(U *)(mode + 112) = 480; *(U *)(mode + 104) = 8;
    if (ChangeDisplaySettingsA(mode, 4) != 0) /* CDS_FULLSCREEN */
        ExitProcess(7603);
    const U hwnd = CreateWindowExA(8, "NEO98MODEPOINTER", "", 0x90000000u, 0, 0, 640, 480, 0, 0, klass[4], 0);
    if (!hwnd)
        ExitProcess(7604);
    ShowWindow(hwnd, 5);
    UpdateWindow(hwnd);
    SetCursor(arrow);
    records[1] = 1;
    pump_until(&records[2], 600000);
    if (ShowCursor(0) != -1)
        ExitProcess(7605);
    records[1] = 2;
    pump_until(&records[3], 600000);
    ExitProcess(records[3] ? 0 : 7606);
}
