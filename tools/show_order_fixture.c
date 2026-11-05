/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The messages of a minimize, maximize and restore, in order.
   USER changes the state of a window in these steps: for a maximize
   WM_GETMINMAXINFO, for a minimized window that
   opens WM_QUERYOPEN, then the style bits and
   one DeferWindowPos with SWP_STATECHANGED (8000h),
   SWP_FRAMECHANGED and, out of the minimized state, SWP_NOCOPYBITS. The
   window therefore gets WM_WINDOWPOSCHANGING, WM_NCCALCSIZE and
   WM_WINDOWPOSCHANGED; DefWindowProc asks WM_GETMINMAXINFO again while it
   clamps the size in WM_WINDOWPOSCHANGING and sends WM_MOVE
   and WM_SIZE out of WM_WINDOWPOSCHANGED. There is no WM_SHOWWINDOW
   on this path.
   A window V takes the activation while the window W is minimized. W writes
   down what it gets; each step ends with a line "= name iconic zoomed".
   D:\SHOWORD.TXT. tools/test_show_order_native.py. */
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
API int PeekMessageA(U *, U, U, U, U);
API int DispatchMessageA(const U *);
API int ShowWindow(U, int);
API int IsIconic(U);
API int IsZoomed(U);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);

struct Class { U style, proc; int class_extra, window_extra; U instance, icon, cursor, brush; const char *menu, *name; };
static U w;
static int recording;
static char text[8000];

static void Line(const char *line)
{
    if (lstrlenA(text) + lstrlenA(line) + 3 < (int)sizeof text) wsprintfA(text + lstrlenA(text), "%s\r\n", line);
}
static U __stdcall Procedure(U window, U message, U wparam, U lparam)
{
    char line[80];
    if (window == w && recording)
    {
        line[0] = 0;
        if (message == 0x24) wsprintfA(line, "MINMAX");
        if (message == 0x13) wsprintfA(line, "QUERYOPEN");
        if (message == 0x46) wsprintfA(line, "CHANGING %04x", ((U *)lparam)[6] & 0xfffb);
        if (message == 0x47) wsprintfA(line, "CHANGED %04x", ((U *)lparam)[6] & 0x8020);
        if (message == 0x83) wsprintfA(line, "NCCALC");
        if (message == 0x03) wsprintfA(line, "MOVE");
        if (message == 0x05) wsprintfA(line, "SIZE %u", wparam);
        if (message == 0x18) wsprintfA(line, "SHOWWINDOW %u", wparam);
        if (message == 0x06) wsprintfA(line, "ACTIVATE %u", wparam & 0xffff);
        if (line[0]) Line(line);
    }
    return DefWindowProcA(window, message, wparam, lparam);
}
static void Pump(void)
{
    U message[7];
    while (PeekMessageA(message, 0, 0, 0, 1)) DispatchMessageA(message);
}
static void Step(const char *name, int command)
{
    char line[80];
    ShowWindow(w, command);
    Pump();
    wsprintfA(line, "= %s iconic=%d zoomed=%d", name, IsIconic(w) != 0, IsZoomed(w) != 0);
    Line(line);
}
void mainCRTStartup(void)
{
    struct Class klass = {0};
    U written = 0, file;
    klass.proc = (U)Procedure;
    klass.instance = 0x400000;
    klass.brush = 6;
    klass.name = "Neo98ShowOrder";
    RegisterClassA(&klass);
    CreateWindowExA(0, klass.name, "V", 0x10cf0000u, 400, 50, 200, 150, 0, 0, 0x400000, 0);
    w = CreateWindowExA(0, klass.name, "W", 0x10cf0000u, 50, 50, 300, 200, 0, 0, 0x400000, 0);
    Pump();
    recording = 1;
    Step("maximize", 3);
    Step("restore", 9);
    Step("minimize", 6);
    Step("open", 9);
    Step("maximize again", 3);
    Step("minimize maximized", 6);
    Step("open maximized", 9);
    Step("maximize maximized", 3);
    Step("show minimized", 2);
    Step("show normal", 1);
    file = CreateFileA("D:\\SHOWORD.TXT", 0x40000000u, 0, 0, 2, 0x80, 0);
    WriteFile(file, text, (U)lstrlenA(text), &written, 0);
    CloseHandle(file);
    ExitProcess(0);
}
