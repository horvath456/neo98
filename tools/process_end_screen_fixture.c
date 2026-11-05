/* SPDX-License-Identifier: GPL-2.0-or-later */
/* a program takes the screen (full-screen window, 640x480x8 through
 * ChangeDisplaySettings), sets its own pointer, hides it and ends without any
 * cleanup, the way a game does that is stopped by its protector. The parent
 * stays and covers the screen with a white window under an arrow. After the
 * child has gone, Win98 shows the desktop mode again and the pointer over the
 * parent's window. tools/test_process_end_screen_native.cpp looks at both.
 *
 *
 * the parent reserves 28 pixels at the bottom with SPI_SETWORKAREA,
 * as the taskbar does. USER keeps those insets when the mode changes:
 * the child sees 0,0,640,452 in 640x480 and fails with 7705
 * otherwise, and the parent sees 0,0,800,572 again after the child has gone.
 *
 * Records of the parent at 0x30000000: [1] stage (1 ready, 2 child gone),
 * [2] go from the runner, [3] exit code of the child, [4] end, [5..8] the
 * work area after the child. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U GetModuleHandleA(const char *);
API char *GetCommandLineA(void);
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
API U CreateProcessA(const char *, char *, U, U, U, U, const char *, const char *, U *, U *);
API U WaitForSingleObject(U, U);
API U MsgWaitForMultipleObjects(U, const U *, U, U, U);
API U GetExitCodeProcess(U, U *);
API U CloseHandle(U);
API U GetTickCount(void);
API U SystemParametersInfoA(U, U, void *, U);
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

static U window(const char *name, U cursor, U brush)
{
    U klass[10] = {0};
    klass[1] = (U)(void *)window_procedure;
    klass[4] = GetModuleHandleA(0);
    klass[6] = cursor;
    klass[7] = brush;
    klass[9] = (U)(void *)name;
    if (!RegisterClassA(klass))
        return 0;
    const U hwnd = CreateWindowExA(8, name, name, 0x90000000u, 0, 0, 800, 600, 0, 0, klass[4], 0);
    if (hwnd)
    {
        ShowWindow(hwnd, 5);
        UpdateWindow(hwnd);
    }
    return hwnd;
}

/* The child: screen, pointer and mode, then ExitProcess with nothing undone. */
static void child(void)
{
    static int never;
    const U cross = LoadCursorA(0, 32515);
    if (!window("NEO98ENDCHILD", cross, GetStockObject(4)))
        ExitProcess(7701);
    unsigned char mode[156] = {0};
    *(unsigned short *)(mode + 36) = 156;
    if (!EnumDisplaySettingsA(0, 0xffffffff, mode))
        ExitProcess(7702);
    *(U *)(mode + 108) = 640; *(U *)(mode + 112) = 480; *(U *)(mode + 104) = 8;
    if (ChangeDisplaySettingsA(mode, 4) != 0) /* CDS_FULLSCREEN */
        ExitProcess(7703);
    int work[4] = {0};
    if (!SystemParametersInfoA(0x30, 0, work, 0) || work[0] != 0 || work[1] != 0 || work[2] != 640 ||
        work[3] != 452)
        ExitProcess(7705);
    SetCursor(cross);
    if (ShowCursor(0) != -1)
        ExitProcess(7704);
    pump_until(&never, 200);
    ExitProcess(0xffffffffu);
}

void mainCRTStartup(void)
{
    const char *line = GetCommandLineA();
    if (line[0] == 'C')
        child();
    records = VirtualAlloc((void *)RECORDS, 65536, 0x3000, 4);
    if (records != (int *)RECORDS)
        ExitProcess(7600);
    const U arrow = LoadCursorA(0, 32512);
    if (!window("NEO98ENDPARENT", arrow, GetStockObject(0)))
        ExitProcess(7601);
    SetCursor(arrow);
    int work[4] = {0, 0, 800, 572};
    if (!SystemParametersInfoA(0x2f, 0, work, 0))
        ExitProcess(7606);
    records[1] = 1;
    pump_until(&records[2], 20000);
    U si[17] = {68}, pi[4] = {0};
    char command[] = "CHILD";
    if (!CreateProcessA("C:\\ENDSCREEN.EXE", command, 0, 0, 0, 0, 0, 0, si, pi))
        ExitProcess(7602);
    /* The parent keeps its queue going: the child's activation sends to it. */
    U message[8];
    const U start = GetTickCount();
    while (WaitForSingleObject(pi[0], 0) != 0)
    {
        if (GetTickCount() - start > 20000)
            ExitProcess(7603);
        MsgWaitForMultipleObjects(1, pi, 0, 100, 0xff);
        while (PeekMessageA(message, 0, 0, 0, 1))
            DispatchMessageA(message);
    }
    U code = 0;
    if (!GetExitCodeProcess(pi[0], &code))
        ExitProcess(7604);
    CloseHandle(pi[0]);
    CloseHandle(pi[1]);
    records[3] = (int)code;
    SystemParametersInfoA(0x30, 0, (void *)&records[5], 0);
    records[1] = 2;
    pump_until(&records[4], 20000);
    ExitProcess(records[4] ? 0 : 7605);
}
