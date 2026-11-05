/* SPDX-License-Identifier: GPL-2.0-or-later */
/* What the foreground is after its window was hidden.
   USER, GetForegroundWindow, answers the active window of the
   foreground queue and does not look at WS_VISIBLE. ShowWindow(SW_HIDE)
   hands the activation on only for the active window of the calling queue
   (with code 3); a thread that hides the window of
   another thread changes nothing about the activation, and Windows 98 leaves
   the window active when it finds no other one.
   Two top-level windows W1 and W2 of the main thread:
     start W2 is the foreground
     own the main thread hides W2: W1 takes over
     foreign a second thread hides W1: W1 stays the active window of its
              queue and the foreground window, hidden
     last W1 is shown again and the main thread hides it, with no other
              window left to activate: it stays, too
   D:\HIDEFG.TXT gets one line per step: the foreground and the active window
   as 1, 2, - (none) or ? and whether W1 and W2 are visible.
   tools/test_hide_foreground_native.py. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API void Sleep(U);
API U CreateThread(void *, U, void *, void *, U, U *);
API U CreateFileA(const char *, U, U, U, U, U, U);
API U WriteFile(U, const void *, U, U *, U);
API U CloseHandle(U);
API int lstrlenA(const char *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefWindowProcA(U, U, U, U);
API int PeekMessageA(U *, U, U, U, U);
API int DispatchMessageA(const U *);
API int ShowWindow(U, int);
API int IsWindowVisible(U);
API U GetActiveWindow(void);
API U GetForegroundWindow(void);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);

struct Class { U style, proc; int class_extra, window_extra; U instance, icon, cursor, brush; const char *menu, *name; };
static U w1, w2;
static char text[600];
static volatile int hidden;
static char foreign_foreground;

static char Name(U window) { return !window ? '-' : window == w1 ? '1' : window == w2 ? '2' : '?'; }
static void Pump(void)
{
    U message[7];
    while (PeekMessageA(message, 0, 0, 0, 1)) DispatchMessageA(message);
}
static void Line(const char *step)
{
    wsprintfA(text + lstrlenA(text), "%s foreground=%c active=%c visible=%d%d\r\n", step, Name(GetForegroundWindow()),
              Name(GetActiveWindow()), IsWindowVisible(w1) != 0, IsWindowVisible(w2) != 0);
}
static U __stdcall Other(void *unused)
{
    (void)unused;
    ShowWindow(w1, 0);
    foreign_foreground = Name(GetForegroundWindow());
    hidden = 1;
    return 0;
}
void mainCRTStartup(void)
{
    struct Class klass = {0};
    U written = 0, file, id = 0;
    klass.proc = (U)DefWindowProcA;
    klass.instance = 0x400000;
    klass.name = "Neo98HideForeground";
    RegisterClassA(&klass);
    w1 = CreateWindowExA(0, klass.name, "W1", 0x10c80000u, 20, 20, 200, 150, 0, 0, 0x400000, 0);
    Pump();
    w2 = CreateWindowExA(0, klass.name, "W2", 0x10c80000u, 240, 20, 200, 150, 0, 0, 0x400000, 0);
    Pump();
    Line("start");
    ShowWindow(w2, 0);
    Pump();
    Line("own");
    CloseHandle(CreateThread(0, 0, Other, 0, 0, &id));
    for (int i = 0; i < 300 && !hidden; ++i) { Pump(); Sleep(10); }
    Pump();
    wsprintfA(text + lstrlenA(text), "thread foreground=%c\r\n", foreign_foreground ? foreign_foreground : 'x');
    Line("foreign");
    ShowWindow(w1, 5);
    Pump();
    Line("shown");
    ShowWindow(w1, 0);
    Pump();
    Line("last");
    file = CreateFileA("D:\\HIDEFG.TXT", 0x40000000u, 0, 0, 2, 0x80, 0);
    WriteFile(file, text, (U)lstrlenA(text), &written, 0);
    CloseHandle(file);
    ExitProcess(0);
}
