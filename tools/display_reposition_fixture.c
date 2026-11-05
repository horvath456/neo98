/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest side of tools/test_display_reposition_native.py.
 *
 * USER places every top-level window anew after a display mode
 * change without CDS_FULLSCREEN. Three windows: a sizable window at the
 * bottom right of 800x600, a tool window (never moved) and a
 * maximized window (as large as the new screen, left alone ). The
 * program changes to 640x480 with flags 0, reads the three rectangles, goes
 * back with ChangeDisplaySettings(NULL, 0) and reads them again. Every
 * rectangle goes out as "REPOS <stage> <name> left top right bottom". */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void OutputDebugStringA(const char *);
API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U ShowWindow(U, int);
API U GetWindowRect(U, int *);
API U PeekMessageA(void *, U, U, U, U);
API U DispatchMessageA(const void *);
API U EnumDisplaySettingsA(const char *, U, void *);
API int ChangeDisplaySettingsA(void *, U);

static U __stdcall procedure(U hwnd, U message, U wp, U lp) { return DefWindowProcA(hwnd, message, wp, lp); }

static void pump(void)
{
    U message[8];
    for (int round = 0; round < 50; ++round)
        while (PeekMessageA(message, 0, 0, 0, 1))
            DispatchMessageA(message);
}

static char *number(char *p, int v)
{
    char digits[12];
    int n = 0;
    unsigned u = v < 0 ? (unsigned)-v : (unsigned)v;
    if (v < 0) *p++ = '-';
    do { digits[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    while (n) *p++ = digits[--n];
    return p;
}

static void report(const char *stage, const char *name, U hwnd)
{
    int r[4] = {0, 0, 0, 0};
    GetWindowRect(hwnd, r);
    char line[96], *p = line;
    for (const char *s = "REPOS "; *s;) *p++ = *s++;
    while (*stage) *p++ = *stage++;
    *p++ = ' ';
    while (*name) *p++ = *name++;
    for (int i = 0; i < 4; ++i) { *p++ = ' '; p = number(p, r[i]); }
    *p++ = '\n';
    *p = 0;
    OutputDebugStringA(line);
}

void mainCRTStartup(void)
{
    U klass[10] = {0};
    klass[1] = (U)(void *)procedure;
    klass[4] = GetModuleHandleA(0);
    klass[9] = (U)(void *)"ReposWindow";
    if (!RegisterClassA(klass)) ExitProcess(2);
    const U sizable = CreateWindowExA(0, "ReposWindow", "Sizable", 0x10cf0000u, 480, 380, 250, 150, 0, 0, klass[4], 0);
    const U tool = CreateWindowExA(0x80, "ReposWindow", "Tool", 0x10c80000u, 600, 500, 150, 80, 0, 0, klass[4], 0);
    const U maximized = CreateWindowExA(0, "ReposWindow", "Maximized", 0x00cf0000u, 10, 10, 200, 100, 0, 0, klass[4], 0);
    if (!sizable || !tool || !maximized) ExitProcess(3);
    ShowWindow(maximized, 3);
    pump();
    report("before", "sizable", sizable);
    report("before", "tool", tool);
    report("before", "maximized", maximized);
    unsigned char mode[156] = {0};
    *(unsigned short *)(mode + 36) = 156;
    if (!EnumDisplaySettingsA(0, 0xffffffffu, mode)) ExitProcess(4);
    *(U *)(mode + 40) = 0x180000; /* DM_PELSWIDTH | DM_PELSHEIGHT */
    *(U *)(mode + 108) = 640;
    *(U *)(mode + 112) = 480;
    if (ChangeDisplaySettingsA(mode, 0) != 0) ExitProcess(5);
    pump();
    report("small", "sizable", sizable);
    report("small", "tool", tool);
    report("small", "maximized", maximized);
    if (ChangeDisplaySettingsA(0, 0) != 0) ExitProcess(6);
    pump();
    report("back", "sizable", sizable);
    report("back", "tool", tool);
    report("back", "maximized", maximized);
    ExitProcess(0);
}
