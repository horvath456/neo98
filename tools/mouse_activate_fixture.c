/* SPDX-License-Identifier: GPL-2.0-or-later */
/* WM_MOUSEACTIVATE. Seven top-level windows in a row and one child:
   H and A pass the message to DefWindowProc, B answers MA_ACTIVATEANDEAT, C
   MA_NOACTIVATE, D MA_NOACTIVATEANDEAT; E has a child K whose procedure
   passes the message on, so DefWindowProc asks E, which answers
   MA_NOACTIVATE; a press on Q ends the program. Every WM_MOUSEACTIVATE, its
   answer where DefWindowProc gave it, every WM_ACTIVATE that activates, every
   press and every change of the active, foreground and focus window goes
   into D:\MOUSEACT.TXT in the order it happened.
   The right and the middle press are written down as RDOWN, MDOWN and
   NCRDOWN. A window T of a second thread answers MA_NOACTIVATE and
   notes whether it was asked on its own thread. The program starts itself
   once more with the argument "other": that process shows P (MA_NOACTIVATE)
   and R (DefWindowProc) without taking the foreground and keeps its own
   record in D:\MOUSEAC2.TXT; it ends when the first process is gone.
   tools/test_mouse_activate_native.py. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U CreateFileA(const char *, U, U, U, U, U, U);
API U WriteFile(U, const void *, U, U *, U);
API U CloseHandle(U);
API int lstrlenA(const char *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefWindowProcA(U, U, U, U);
API int GetMessageA(U *, U, U, U);
API int DispatchMessageA(const U *);
API void PostQuitMessage(int);
API U SetTimer(U, U, U, U);
API U GetActiveWindow(void);
API U GetForegroundWindow(void);
API U GetFocus(void);
API U SetFocus(U);
API int ShowWindow(U, int);
API U FindWindowA(const char *, const char *);
API U GetCurrentThreadId(void);
API U CreateThread(void *, U, void *, void *, U, U *);
API char *GetCommandLineA(void);
API U WinExec(const char *, U);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);

struct Class { U style, proc; int class_extra, window_extra; U instance, icon, cursor, brush; const char *menu, *name; };
static U windows[10], main_thread, other_thread;
static const char *letters = "HABCDEQKTS";
static char text[6000], state[16];
static int other;

static char Letter(U window)
{
    for (int i = 0; i < 10 && letters[i]; ++i)
        if (window && windows[i] == window) return letters[i];
    return window ? '?' : '-';
}
static void Line(const char *line)
{
    if (lstrlenA(text) + lstrlenA(line) + 3 < (int)sizeof text) wsprintfA(text + lstrlenA(text), "%s\r\n", line);
}
static void Save(const char *path)
{
    U written = 0, file = CreateFileA(path, 0x40000000u, 0, 0, 2, 0x80, 0);
    WriteFile(file, text, (U)lstrlenA(text), &written, 0);
    CloseHandle(file);
}
static void State(void)
{
    char now[16];
    if (other || GetCurrentThreadId() != main_thread) return;
    wsprintfA(now, "%c%c%c", Letter(GetActiveWindow()), Letter(GetForegroundWindow()), Letter(GetFocus()));
    if (now[0] != state[0] || now[1] != state[1] || now[2] != state[2])
    {
        char line[40];
        wsprintfA(state, "%s", now);
        wsprintfA(line, "STATE %s", now);
        Line(line);
    }
}
static U __stdcall Procedure(U window, U message, U wparam, U lparam)
{
    const char who = Letter(window);
    char line[80];
    if (message == 0x21) /* WM_MOUSEACTIVATE */
    {
        U answer;
        State();
        if (who == 'T') wsprintfA(line, "MA %c top=%c lp=%08x thread=%d", who, Letter(wparam), lparam, GetCurrentThreadId() == other_thread);
        else wsprintfA(line, "MA %c top=%c lp=%08x", who, Letter(wparam), lparam);
        Line(line);
        if (other) Save("D:\\MOUSEAC2.TXT");
        if (who == 'B') return 2; /* MA_ACTIVATEANDEAT */
        if (who == 'C' || who == 'T' || who == 'P' || who == 'S') return 3; /* MA_NOACTIVATE */
        if (who == 'D') return 4; /* MA_NOACTIVATEANDEAT */
        if (who == 'E') return 3;
        answer = DefWindowProcA(window, message, wparam, lparam);
        wsprintfA(line, "MAR %c %u", who, answer);
        Line(line);
        if (other) Save("D:\\MOUSEAC2.TXT");
        return answer;
    }
    if (message == 0x06 && (wparam & 0xffff)) /* WM_ACTIVATE, becoming active */
    {
        wsprintfA(line, "ACT %c wa=%u", who, wparam & 0xffff);
        Line(line);
        if (other) Save("D:\\MOUSEAC2.TXT");
    }
    if (message == 0x204 || message == 0x207 || message == 0xa4) /* right, middle, right on the frame */
    {
        if (message == 0xa4) wsprintfA(line, "NCRDOWN %c hit=%u", who, wparam);
        else wsprintfA(line, "%s %c", message == 0x204 ? "RDOWN" : "MDOWN", who);
        Line(line);
        return 0;
    }
    if (message == 0xa5 || message == 0x7b) return 0; /* no system menu from the right button in this test */
    if (message == 0x201 || message == 0x203)
    {
        wsprintfA(line, "DOWN %c", who);
        Line(line);
        if (other) Save("D:\\MOUSEAC2.TXT");
        if (who == 'S') SetFocus(window); /* explicit activation after MA_NOACTIVATE */
        if (who == 'Q')
        {
            State();
            Save("D:\\MOUSEACT.TXT");
            PostQuitMessage(0);
        }
        return 0;
    }
    if (message == 0xa1) /* WM_NCLBUTTONDOWN */
    {
        wsprintfA(line, "NCDOWN %c hit=%u", who, wparam);
        Line(line);
    }
    if (message == 0x113) /* WM_TIMER */
    {
        State();
        if (other && !FindWindowA("Neo98MouseActivate", "Q")) PostQuitMessage(0);
    }
    return DefWindowProcA(window, message, wparam, lparam);
}
static struct Class klass;
/* T lives on a second thread: the question must reach it there. */
static U __stdcall Second(void *unused)
{
    U message[7];
    (void)unused;
    other_thread = GetCurrentThreadId();
    windows[8] = CreateWindowExA(0, klass.name, "T", 0x00c80000u, 140, 200, 120, 100, 0, 0, 0x400000, 0);
    ShowWindow(windows[8], 4); /* SW_SHOWNOACTIVATE */
    while (GetMessageA(message, 0, 0, 0) > 0) DispatchMessageA(message);
    return 0;
}
void mainCRTStartup(void)
{
    U message[7], id = 0;
    for (const char *p = GetCommandLineA(); *p; ++p)
        if (p[0] == 'o' && p[1] == 't' && p[2] == 'h' && p[3] == 'e' && p[4] == 'r') other = 1;
    main_thread = GetCurrentThreadId();
    klass.style = 0;
    klass.proc = (U)Procedure;
    klass.instance = 0x400000;
    klass.brush = 6; /* COLOR_WINDOW + 1 */
    klass.name = other ? "Neo98MouseActivateOther" : "Neo98MouseActivate";
    RegisterClassA(&klass);
    if (other)
    {
        letters = "PR";
        for (int i = 0; i < 2; ++i)
        {
            char title[2] = {letters[i], 0};
            windows[i] = CreateWindowExA(0, klass.name, title, 0x00c80000u, 270 + i * 130, 200, 120, 100, 0, 0, 0x400000, 0);
            ShowWindow(windows[i], 4); /* SW_SHOWNOACTIVATE */
        }
        SetTimer(windows[0], 1, 200, 0);
        while (GetMessageA(message, 0, 0, 0) > 0) DispatchMessageA(message);
        ExitProcess(0);
    }
    for (int i = 0; i < 6; ++i)
    {
        char title[2] = {letters[i], 0};
        windows[i] = CreateWindowExA(0, klass.name, title, 0x10c80000u, 10 + i * 130, 10, 120, 100, 0, 0, 0x400000, 0);
    }
    windows[6] = CreateWindowExA(0, klass.name, "Q", 0x10c80000u, 10, 200, 120, 100, 0, 0, 0x400000, 0);
    windows[7] = CreateWindowExA(0, klass.name, "K", 0x50000000u, 10, 10, 80, 40, windows[5], 0, 0x400000, 0);
    windows[9] = CreateWindowExA(0, klass.name, "S", 0x00c80000u, 530, 200, 120, 100, 0, 0, 0x400000, 0);
    ShowWindow(windows[9], 4);
    CloseHandle(CreateThread(0, 0, Second, 0, 0, &id));
    WinExec("D:\\MOUSEACT.EXE other", 4);
    SetTimer(windows[0], 1, 40, 0);
    State();
    while (GetMessageA(message, 0, 0, 0) > 0) DispatchMessageA(message);
    ExitProcess(0);
}
