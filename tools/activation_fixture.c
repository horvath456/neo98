/* SPDX-License-Identifier: GPL-2.0-or-later
 * Own x86 ABI/callback contract. USER activation decisions execute natively on ARM. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DestroyWindow(U);
API U DefWindowProcA(U, U, U, U);
API U SetActiveWindow(U);
API U GetActiveWindow(void);
API U SetForegroundWindow(U);
API U GetForegroundWindow(void);
API U GetFocus(void);
API U SetFocus(U);
API U GetLastActivePopup(U);
API U ShowWindow(U, U);
API U IsIconic(U);
API U IsZoomed(U);
API U IsWindow(U);
API U CloseWindow(U);
API U OpenIcon(U);
API U GetWindowPlacement(U, U *);
API U SetWindowPlacement(U, const U *);
API U GetWindowRect(U, void *);
API U SendMessageA(U, U, U, U);
API U PeekMessageA(void *, U, U, U, U);
API U DispatchMessageA(void *);
API void SwitchToThisWindow(U, U);
API void ExitProcess(U);
static U recording, count, events[64][4], veto_nc, veto_open, destroy_during, target, redirect;
static void check(int yes, U code)
{
    if (!yes)
        ExitProcess(code);
}
static U __stdcall proc(U h, U m, U w, U l)
{
    if (recording && (m == 0x86 || m == 6 || m == 7 || m == 8))
    {
        check(count < 64, 2200);
        events[count][0] = h;
        events[count][1] = m;
        events[count][2] = w;
        events[count++][3] = l;
    }
    if (m == 0x86 && !w)
    {
        if (destroy_during)
        {
            destroy_during = 0;
            check(DestroyWindow(target), 2201);
        }
        if (redirect)
        {
            U next = redirect;
            redirect = 0;
            SetForegroundWindow(next);
        }
        if (veto_nc)
            return 0;
    }
    if (m == 0x13 && veto_open)
        return 0;
    return DefWindowProcA(h, m, w, l);
}
static void event(U i, U h, U m, U w, U l)
{
    check(i < count && events[i][0] == h && events[i][1] == m && events[i][2] == w && events[i][3] == l,
          2202 + i);
}
void run_activation_test(U module)
{
    struct
    {
        U style;
        U(__stdcall *proc)(U, U, U, U);
        int ce, we;
        U instance, icon, cursor, background;
        const char *menu, *name;
    } klass = {0, proc, 0, 0, module, 0, 0, 16, 0, "NEO98ActivationContract"};
    check(RegisterClassA(&klass), 2210);
    U a = CreateWindowExA(0, klass.name, "Activation one", 0x10cf0000, 40, 60, 300, 200, 0, 0, module, 0);
    U b = CreateWindowExA(0, klass.name, "Activation two", 0x10cf0000, 360, 100, 300, 200, 0, 0, module, 0);
    U c = CreateWindowExA(0, klass.name, "Owned popup", 0x90cf0000, 100, 150, 240, 180, b, 0, module, 0);
    check(a && b && c, 2211);
    check(SetForegroundWindow(a) && GetActiveWindow() == a && GetFocus() == a, 2212);
    recording = 1;
    count = 0;
    check(SetActiveWindow(b) == a && GetActiveWindow() == b && GetForegroundWindow() == b && GetFocus() == b,
          2213);
    check(count == 6, 2214);
    event(0, a, 0x86, 0, 0);
    event(1, a, 6, 0, b);
    event(2, b, 0x86, 1, 0);
    event(3, b, 6, 1, a);
    event(4, a, 8, b, 0);
    event(5, b, 7, a, 0);
    recording = 0;
    check(SetForegroundWindow(c) && GetLastActivePopup(b) == c && GetLastActivePopup(c) == c, 2215);
    check(SetForegroundWindow(b), 2216);
    veto_nc = 1;
    check(!SetForegroundWindow(a) && GetActiveWindow() == b && GetFocus() == b, 2217);
    veto_nc = 0;
    target = a;
    destroy_during = 1;
    check(!SetForegroundWindow(a) && !IsWindow(a) && GetActiveWindow() == b, 2218);
    // Destruction during deactivation does not turn a recycled native HWND into the old target.
    a = CreateWindowExA(0, klass.name, "Replacement", 0x10cf0000, 40, 60, 300, 200, 0, 0, module, 0);
    check(a && a != target, 2219);
    // Visible creation activates the replacement; restore the deactivation source.
    check(SetForegroundWindow(b) && GetActiveWindow() == b, 2240);
    redirect = c;
    check(!SetForegroundWindow(a) && GetActiveWindow() == c, 2220);
    check(SetForegroundWindow(b), 2221);
    U placement[11] = {44};
    check(GetWindowPlacement(b, placement) && placement[2] == 1, 2222);
    ShowWindow(b, 3);
    check(IsZoomed(b) && !IsIconic(b), 2223);
    ShowWindow(b, 8);
    check(IsZoomed(b), 2238);
    U rect[4];
    /* USER inflates the monitor rectangle by the window's own frame in
       border units before WM_GETMINMAXINFO, so a captioned, sized
       window maximizes with its frame off screen; only its client area covers
       the monitor. Caption plus sizing border give two border units per side,
       four pixels with the current metrics. The work-area correction
       leaves that alone while the work area is the whole monitor. Maximizing
       used to return the hardcoded monitor rectangle instead. */
    check(GetWindowRect(b, rect) && (int)rect[0] == -4 && (int)rect[1] == -4 && rect[2] == 804 &&
              rect[3] == 604,
          2224);
    check(CloseWindow(b) && IsIconic(b) && !IsZoomed(b) && IsWindow(b), 2225);
    veto_open = 1;
    check(!OpenIcon(b) && IsIconic(b), 2226);
    veto_open = 0;
    check(OpenIcon(b) && !IsIconic(b) && !IsZoomed(b) && GetActiveWindow() == b, 2227);
    ShowWindow(b, 1);
    check(!IsZoomed(b) && GetWindowRect(b, rect) && rect[0] == placement[7] && rect[1] == placement[8] &&
              rect[2] == placement[9] && rect[3] == placement[10],
          2228);
    ShowWindow(b, 7);
    check(IsIconic(b) && !GetFocus(), 2229);
    ShowWindow(b, 8);
    check(IsIconic(b), 2239);
    SwitchToThisWindow(b, 1);
    check(IsIconic(b), 2230);
    U message[7];
    check(PeekMessageA(message, b, 0x112, 0x112, 1) && message[2] == 0xf120, 2231);
    DispatchMessageA(message);
    check(!IsIconic(b) && GetActiveWindow() == b && GetFocus() == b, 2232);
    ShowWindow(b, 7);
    SwitchToThisWindow(b, 0);
    check(IsIconic(b) && !PeekMessageA(message, b, 0x112, 0x112, 1), 2233);
    ShowWindow(b, 1);
    placement[2] = 3;
    check(SetWindowPlacement(b, placement) && IsZoomed(b), 2234);
    placement[2] = 2;
    check(SetWindowPlacement(b, placement) && IsIconic(b), 2235);
    placement[2] = 1;
    check(SetWindowPlacement(b, placement) && !IsIconic(b) && !IsZoomed(b), 2236);
    check(DestroyWindow(c) && DestroyWindow(a) && DestroyWindow(b), 2237);
}
