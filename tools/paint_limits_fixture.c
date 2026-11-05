/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Three limits Dev-C++ ran into, in one program:
 * 1. a thousand compatible bitmaps at the same time; the port had room for
 * about two hundred and answered the rest with error 87;
 * 2. a modal loop inside WM_PAINT; while that paint was open no BUTTON of any
 * thread was painted;
 * 3. a window whose DC cannot be had; BeginPaint left the update region in
 * place, so WM_PAINT came again and again;
 * 4. UpdateWindow on a parent paints its children at once;
 * the port painted the parent alone, so a splash picture in a child panel
 * stayed grey while the thread kept loading.
 * Results go to the shared buffer; tools/test_paint_limits_native.cpp reads
 * them and checks the button from the outside. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U GetModuleHandleA(const char *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U ShowWindow(U, int);
API U UpdateWindow(U);
API U PeekMessageA(void *, U, U, U, U);
API U DispatchMessageA(const void *);
API U BeginPaint(U, void *);
API U EndPaint(U, const void *);
API U GetUpdateRect(U, void *, U);
API U GetDC(U);
API U ReleaseDC(U, U);
API U CreateCompatibleBitmap(U, int, int);
API U DeleteObject(U);
API U GetLastError(void);
API U GetTickCount(void);
API void Sleep(U);
API void *VirtualAlloc(void *, U, U, U);
API void ExitProcess(U);

#define RECORDS 0x30000000u
#define BITMAPS 1000
static volatile int *records;
static U klass[10];
static char class_name[] = "NEO98LIMITS", button_class[] = "BUTTON";
static U instance, nested_done, big, splash_child;
static U bitmaps[BITMAPS];

static void pump(void)
{
    U message[8];
    while (PeekMessageA(message, 0, 0, 0, 1))
        DispatchMessageA(message);
}

static U __stdcall window_procedure(U hwnd, U message, U wp, U lp)
{
    if (message == 0x0f) /* WM_PAINT */
    {
        U paint[16];
        const U dc = BeginPaint(hwnd, paint);
        if (hwnd == splash_child)
            records[9]++;
        else if (hwnd == big)
        {
            records[3]++;
            records[4] = (int)dc;
        }
        else if (!nested_done)
        {
            nested_done = 1;
            /* A second window with a button, and a modal loop while this
             * paint is still open, the way Dev-C++ shows its tip dialog. */
            const U other = CreateWindowExA(0, class_name, class_name, 0x90000000u, 400, 300, 200, 100, 0, 0, instance, 0);
            const U button = CreateWindowExA(0, button_class, "OK", 0x50000000u, 10, 10, 100, 30, other, 1, instance, 0);
            records[2] = (int)button;
            records[0] = 1;
            const U start = GetTickCount();
            while (!records[6] && GetTickCount() - start < 20000)
            {
                pump();
                Sleep(1);
            }
        }
        EndPaint(hwnd, paint);
        return 0;
    }
    return DefWindowProcA(hwnd, message, wp, lp);
}

void mainCRTStartup(void)
{
    records = VirtualAlloc((void *)RECORDS, 65536, 0x3000, 4);
    if (records != (int *)RECORDS)
        ExitProcess(7700);
    instance = GetModuleHandleA(0);
    const U screen = GetDC(0);
    U made = 0;
    for (U i = 0; i < BITMAPS; ++i)
        if ((bitmaps[i] = CreateCompatibleBitmap(screen, 16, 16)) != 0)
            ++made;
        else if (!records[7])
            records[7] = (int)GetLastError();
    records[1] = (int)made;
    for (U i = 0; i < BITMAPS; ++i)
        if (bitmaps[i])
            DeleteObject(bitmaps[i]);
    ReleaseDC(0, screen);
    klass[1] = (U)(void *)window_procedure;
    klass[4] = instance;
    klass[9] = (U)(void *)class_name;
    if (!RegisterClassA(klass))
        ExitProcess(7701);
    const U main = CreateWindowExA(0, class_name, class_name, 0x90000000u, 0, 0, 300, 200, 0, 0, instance, 0);
    if (!main)
        ExitProcess(7702);
    UpdateWindow(main);
    const U start = GetTickCount();
    while (!nested_done && GetTickCount() - start < 20000)
    {
        pump();
        Sleep(1);
    }
    /* A splash: a parent with a child, shown and updated without a message
     * loop. The child must have had its WM_PAINT when UpdateWindow returns. */
    const U splash = CreateWindowExA(0, class_name, class_name, 0x90000000u, 100, 100, 200, 150, 0, 0, instance, 0);
    splash_child = CreateWindowExA(0, class_name, class_name, 0x50000000u, 10, 10, 100, 80, splash, 2, instance, 0);
    nested_done = 2;
    UpdateWindow(splash);
    records[10] = records[9];
    /* Wider than any surface the port gives a window. */
    big = CreateWindowExA(0, class_name, class_name, 0x90000000u, 0, 0, 5000, 100, 0, 0, instance, 0);
    if (!big)
        ExitProcess(7703);
    for (U i = 0; i < 200; ++i)
    {
        pump();
        Sleep(1);
    }
    U rect[4];
    records[5] = (int)GetUpdateRect(big, rect, 0);
    records[8] = 1;
    ExitProcess(0);
}
