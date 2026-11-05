/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The show command of STARTUPINFO is used up by the first show of a captioned
   top-level window without an owner, whatever command that show names (USER); only SW_SHOWNORMAL and SW_SHOW are replaced by it. Without
   an argument the program starts itself twice with WinExec(..., SW_MAXIMIZE):
   "explicit" shows its first window with SW_SHOWNOACTIVATE and a second one
   with SW_SHOW, "normal" shows the first with SW_SHOWNORMAL. Each writes which
   of its two windows ended up maximized.
   tools/test_startup_show_native.py. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API void Sleep(U);
API char *GetCommandLineA(void);
API U WinExec(const char *, U);
API U CreateFileA(const char *, U, U, U, U, U, U);
API U WriteFile(U, const void *, U, U *, U);
API U CloseHandle(U);
API int lstrlenA(const char *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefWindowProcA(U, U, U, U);
API int ShowWindow(U, int);
API int IsZoomed(U);
API int DestroyWindow(U);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);

struct Class { U style, proc; int class_extra, window_extra; U instance, icon, cursor, brush; const char *menu, *name; };

void mainCRTStartup(void)
{
    struct Class klass = {0};
    char text[80];
    const char *line = GetCommandLineA();
    int explicit_show = 0, normal = 0;
    U first, second, written = 0, file;
    for (const char *p = line; *p; ++p)
    {
        if (p[0] == 'e' && p[1] == 'x' && p[2] == 'p' && p[3] == 'l') explicit_show = 1;
        if (p[0] == 'n' && p[1] == 'o' && p[2] == 'r' && p[3] == 'm') normal = 1;
    }
    if (!explicit_show && !normal)
    {
        WinExec("D:\\SHOW.EXE explicit", 3);
        WinExec("D:\\SHOW.EXE normal", 3);
        Sleep(1500);
        ExitProcess(0);
    }
    klass.proc = (U)DefWindowProcA;
    klass.instance = 0x400000;
    klass.name = "Neo98StartupShow";
    RegisterClassA(&klass);
    first = CreateWindowExA(0, klass.name, "first", 0x00cf0000u, 20, 20, 200, 150, 0, 0, 0x400000, 0);
    second = CreateWindowExA(0, klass.name, "second", 0x00c80000u, 240, 20, 200, 150, 0, 0, 0x400000, 0);
    ShowWindow(first, explicit_show ? 4 : 1); /* SW_SHOWNOACTIVATE or SW_SHOWNORMAL */
    ShowWindow(second, 5);                    /* SW_SHOW */
    wsprintfA(text, "first=%d\r\nsecond=%d\r\n", IsZoomed(first) != 0, IsZoomed(second) != 0);
    file = CreateFileA(explicit_show ? "D:\\EXPLICIT.TXT" : "D:\\NORMAL.TXT", 0x40000000u, 0, 0, 2, 0x80, 0);
    WriteFile(file, text, (U)lstrlenA(text), &written, 0);
    CloseHandle(file);
    DestroyWindow(second);
    DestroyWindow(first);
    ExitProcess(0);
}
