/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest for the WinEvents USER raises itself. An out-of-context hook
   over every event records what arrives at the next PeekMessage while the
   program creates a window, shows it, renames it, moves it, takes and gives
   back the capture, sets the focus and destroys the window. USER.EXE raises
   them (CREATE), (SHOW), (NAMECHANGE),
   (LOCATIONCHANGE), and (CAPTURESTART, CAPTUREEND),
   (FOCUS), (FOREGROUND), (HIDE) and (DESTROY).
   Exit code zero means every check held; codes 4780..4799. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API U RegisterClassA(const U *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U ShowWindow(U, int);
API U SetWindowTextA(U, const char *);
API U MoveWindow(U, int, int, int, int, U);
API U SetCapture(U);
API U ReleaseCapture(void);
API U SetFocus(U);
API U DestroyWindow(U);
API U DefWindowProcA(U, U, U, U);
API U PeekMessageA(U *, U, U, U, U);
API U DispatchMessageA(const U *);
API U SetWinEventHook(U, U, U, void (__stdcall *)(U, U, U, U, U, U, U), U, U, U);
API U UnhookWinEvent(U);

static U failure;
static void check(int ok, U code)
{
    if (!ok && !failure)
        failure = code;
}
static U __stdcall procedure(U h, U m, U w, U l) { return DefWindowProcA(h, m, w, l); }
static U events[64], windows[64], objects[64], count;
static void __stdcall hook(U handle, U event, U hwnd, U object, U child, U thread, U time)
{
    (void)handle; (void)child; (void)thread; (void)time;
    if (count < 64)
    {
        events[count] = event;
        windows[count] = hwnd;
        objects[count] = object;
        ++count;
    }
}
static void pump(void)
{
    U msg[7];
    for (U i = 0; i < 4; ++i)
        while (PeekMessageA(msg, 0, 0, 0, 1))
            DispatchMessageA(msg);
}
static int seen(U event, U hwnd, U object)
{
    for (U i = 0; i < count; ++i)
        if (events[i] == event && windows[i] == hwnd && objects[i] == object)
            return 1;
    return 0;
}
void mainCRTStartup(void)
{
    U wc[10] = {0};
    wc[1] = (U) procedure;
    wc[4] = GetModuleHandleA(0);
    wc[9] = (U) "EventTest";
    check(RegisterClassA(wc) != 0, 4780);
    /* EVENT_MIN..EVENT_MAX, out of context, every process and thread. */
    const U handle = SetWinEventHook(1, 0x7fffffff, 0, hook, 0, 0, 0);
    check(handle != 0, 4781);
    const U w = CreateWindowExA(0, "EventTest", "first", 0x00cf0000, 10, 10, 200, 150, 0, 0, GetModuleHandleA(0), 0);
    check(w != 0, 4782);
    ShowWindow(w, 1);
    SetWindowTextA(w, "second");
    MoveWindow(w, 40, 40, 220, 160, 1);
    SetCapture(w);
    ReleaseCapture();
    SetFocus(w);
    pump();
    check(seen(0x8000, w, 0), 4783);          /* EVENT_OBJECT_CREATE */
    check(seen(0x8002, w, 0), 4784);          /* EVENT_OBJECT_SHOW */
    check(seen(3, w, 0), 4785);               /* EVENT_SYSTEM_FOREGROUND */
    check(seen(0x800c, w, 0), 4786);          /* EVENT_OBJECT_NAMECHANGE */
    check(seen(0x800b, w, 0), 4787);          /* EVENT_OBJECT_LOCATIONCHANGE */
    check(seen(8, w, 0), 4788);               /* EVENT_SYSTEM_CAPTURESTART */
    check(seen(9, w, 0), 4789);               /* EVENT_SYSTEM_CAPTUREEND */
    check(seen(0x8005, w, 0xfffffffc), 4790); /* EVENT_OBJECT_FOCUS on OBJID_CLIENT */
    DestroyWindow(w);
    pump();
    check(seen(0x8003, w, 0), 4791);          /* EVENT_OBJECT_HIDE */
    check(seen(0x8001, w, 0), 4792);          /* EVENT_OBJECT_DESTROY */
    /* CREATE comes before SHOW, HIDE before DESTROY. */
    U create = 99, show = 99, hide = 99, destroy = 99;
    for (U i = 0; i < count; ++i)
    {
        if (windows[i] != w) continue;
        if (events[i] == 0x8000 && create == 99) create = i;
        if (events[i] == 0x8002 && show == 99) show = i;
        if (events[i] == 0x8003 && hide == 99) hide = i;
        if (events[i] == 0x8001 && destroy == 99) destroy = i;
    }
    check(create < show, 4793);
    check(hide < destroy, 4794);
    check(UnhookWinEvent(handle) != 0, 4795);
    ExitProcess(failure);
}
