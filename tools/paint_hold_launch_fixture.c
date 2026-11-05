/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Starts a program while this process holds a paint open.
 *
 * HOLDPNT.EXE D:\DIR\PROGRAM.EXE
 *
 * The launcher starts the program, and after five seconds, when the program
 * has its window and its display mode, shows a window of its own without
 * activating it, opens BeginPaint on it and keeps the paint open until the
 * program ends, pumping its messages meanwhile, as a program does that runs a
 * modal loop inside WM_PAINT. Three seconds later it closes the program the
 * way Alt+F4 does: WM_SYSCOMMAND SC_CLOSE to the foreground window. Win98 changes the
 * display mode whatever other programs paint; the port once refused, and
 * DDRAW's RestoreDisplayMode crashed on that refusal. The exit code is the
 * exit code of the program, or 1000 and up for a failure of the launcher. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U CreateProcessA(const char *, char *, U, U, U, U, U, const char *, U *, U *);
API char *GetCommandLineA(void);
API U WaitForSingleObject(U, U);
API U GetExitCodeProcess(U, U *);
API U GetTickCount(void);
API U GetForegroundWindow(void);
API U PostMessageA(U, U, U, U);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U ShowWindow(U, U);
API U InvalidateRect(U, const void *, U);
API U BeginPaint(U, void *);
API U EndPaint(U, const void *);
API U PeekMessageA(U *, U, U, U, U);
API U DispatchMessageA(const U *);
static char command[260], directory[260];
static U __stdcall procedure(U h, U m, U w, U l) { return DefWindowProcA(h, m, w, l); }
void mainCRTStartup(void)
{
    const char *line = GetCommandLineA();
    if (*line == '"') { ++line; while (*line && *line != '"') ++line; if (*line) ++line; }
    else while (*line && *line != ' ') ++line;
    while (*line == ' ') ++line;
    U n = 0, slash = 0;
    for (; line[n] && n < sizeof command - 1; ++n) { command[n] = directory[n] = line[n]; if (line[n] == '\\') slash = n; }
    if (!n || !slash) ExitProcess(1000);
    directory[slash] = 0;
    U startup[17] = {68}, process[4] = {0};
    if (!CreateProcessA(0, command, 0, 0, 0, 0, 0, directory, startup, process)) ExitProcess(1003);
    U message[7];
    const U start = GetTickCount();
    while (GetTickCount() - start < 5000)
        if (WaitForSingleObject(process[0], 20) == 0) ExitProcess(1005);
    U klass[10] = {0, (U)procedure, 0, 0, 0, 0, 0, 0, 0, (U)"HoldPaint98"};
    if (!RegisterClassA(klass)) ExitProcess(1001);
    U window = CreateWindowExA(0, "HoldPaint98", "hold", 0x00cf0000, 0, 0, 160, 100, 0, 0, 0, 0);
    ShowWindow(window, 8);
    InvalidateRect(window, 0, 1);
    U paint[16];
    if (!BeginPaint(window, paint)) ExitProcess(1002);
    const U held = GetTickCount();
    U closed = 0;
    while (WaitForSingleObject(process[0], 20) != 0)
    {
        while (PeekMessageA(message, 0, 0, 0, 1)) DispatchMessageA(message);
        if (!closed && GetTickCount() - held >= 3000)
        {
            const U target = GetForegroundWindow();
            if (!target || target == window) ExitProcess(1006);
            PostMessageA(target, 0x112, 0xf060, 0);
            closed = 1;
        }
    }
    EndPaint(window, paint);
    U code = 1004;
    GetExitCodeProcess(process[0], &code);
    ExitProcess(code);
}
