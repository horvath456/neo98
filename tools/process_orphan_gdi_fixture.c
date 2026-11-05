/* SPDX-License-Identifier: GPL-2.0-or-later */
/* a process whose parent ended before it ends itself. The parent
 * (C:\ORPHAN.EXE) holds a region and a DC with a clip region, starts a
 * launcher ("LAUNCH") that starts the game ("GAME") and returns at once, as
 * the launcher of a game does. The game outlives the launcher and ends later.
 * Win98 GDI then deletes only what the game owned (GDI for objects
 * whose owner WORD names the task); the parent's region and clip region stay.
 * The parent prints ORPHAN lines; tools/test_process_orphan_gdi.py reads them. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void OutputDebugStringA(const char *);
API void ExitProcess(U);
API char *GetCommandLineA(void);
API U CreateProcessA(const char *, char *, U, U, U, U, const char *, const char *, U *, U *);
API U WaitForSingleObject(U, U);
API U CloseHandle(U);
API U GetTickCount(void);
API void Sleep(U);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefWindowProcA(U, U, U, U);
API U FindWindowA(const char *, const char *);
API U PeekMessageA(void *, U, U, U, U);
API U DispatchMessageA(const void *);
API U DestroyWindow(U);
API U CreateRectRgn(int, int, int, int);
API int GetRgnBox(U, int *);
API int CombineRgn(U, U, U, int);
API U CreateCompatibleDC(U);
API int IntersectClipRect(U, int, int, int, int);
API int ExtSelectClipRgn(U, U, int);
API int GetClipBox(U, int *);
API U DeleteObject(U);
API U DeleteDC(U);
API U CreateBitmap(int, int, U, U, const void *);
API U SelectObject(U, U);

static void number(const char *name, U value)
{
    char text[64];
    U n = 0;
    while (name[n] && n < 40) { text[n] = name[n]; ++n; }
    text[n++] = '=';
    for (int shift = 28; shift >= 0; shift -= 4) text[n++] = "0123456789abcdef"[(value >> shift) & 15];
    text[n] = 0;
    OutputDebugStringA(text);
}
static void pump(U milliseconds)
{
    U message[8];
    const U start = GetTickCount();
    while (GetTickCount() - start < milliseconds)
    {
        while (PeekMessageA(message, 0, 0, 0, 1)) DispatchMessageA(message);
        Sleep(10);
    }
}
static U window(const char *name)
{
    U klass[10] = {0, (U)DefWindowProcA, 0, 0, 0, 0, 0, 6, 0, (U)name};
    RegisterClassA(klass);
    return CreateWindowExA(0, name, name, 0x10000000u, 0, 0, 100, 100, 0, 0, 0, 0);
}
static void game(void)
{
    const U hwnd = window("ORPHANGAME");
    /* Objects of the game's own: they go with it. */
    const U region = CreateRectRgn(0, 0, 10, 10);
    const U dc = CreateCompatibleDC(0);
    IntersectClipRect(dc, 0, 0, 5, 5);
    (void)region;
    pump(1500); /* The launcher is gone by now. */
    DestroyWindow(hwnd);
    ExitProcess(0);
}
static void launch(void)
{
    U si[17] = {68}, pi[4] = {0};
    char line[] = "GAME";
    if (!CreateProcessA("C:\\ORPHAN.EXE", line, 0, 0, 0, 0, 0, 0, si, pi)) ExitProcess(7802);
    CloseHandle(pi[0]);
    CloseHandle(pi[1]);
    ExitProcess(0);
}
void mainCRTStartup(void)
{
    const char *command = GetCommandLineA();
    if (command[0] == 'G') game();
    if (command[0] == 'L') launch();
    window("ORPHANPARENT");
    const U region = CreateRectRgn(1, 2, 30, 40);
    const U dc = CreateCompatibleDC(0);
    const U surface = CreateBitmap(64, 64, 1, 1, 0);
    const U previous = SelectObject(dc, surface);
    IntersectClipRect(dc, 3, 4, 20, 25);
    U si[17] = {68}, pi[4] = {0};
    char line[] = "LAUNCH";
    if (!CreateProcessA("C:\\ORPHAN.EXE", line, 0, 0, 0, 0, 0, 0, si, pi)) ExitProcess(7801);
    const U start = GetTickCount();
    while (WaitForSingleObject(pi[0], 0) != 0)
    {
        if (GetTickCount() - start > 20000) ExitProcess(7803);
        pump(50);
    }
    CloseHandle(pi[0]);
    CloseHandle(pi[1]);
    OutputDebugStringA("ORPHAN launcher ended");
    /* The game runs on without its parent; wait until its window is gone. */
    while (!FindWindowA("ORPHANGAME", 0))
    {
        if (GetTickCount() - start > 20000) ExitProcess(7804);
        pump(20);
    }
    while (FindWindowA("ORPHANGAME", 0))
    {
        if (GetTickCount() - start > 30000) ExitProcess(7805);
        pump(20);
    }
    pump(500);
    OutputDebugStringA("ORPHAN game ended");
    int box[4] = {0};
    number("ORPHAN region_type", (U)GetRgnBox(region, box));
    number("ORPHAN region_right", (U)box[2]);
    number("ORPHAN region_bottom", (U)box[3]);
    number("ORPHAN clip_type", (U)GetClipBox(dc, box));
    number("ORPHAN clip_right", (U)box[2]);
    const U other = CreateRectRgn(0, 0, 5, 5);
    number("ORPHAN combine", (U)CombineRgn(other, other, region, 2)); /* RGN_OR */
    number("ORPHAN select", (U)ExtSelectClipRgn(dc, 0, 5));           /* RGN_COPY with NULL */
    DeleteObject(other);
    DeleteObject(region);
    SelectObject(dc, previous);
    DeleteObject(surface);
    DeleteDC(dc);
    OutputDebugStringA("ORPHAN done");
    ExitProcess(0);
}
