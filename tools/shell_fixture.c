/* SPDX-License-Identifier: GPL-2.0-or-later
 * Own x86 program. Window events originate in native USER on the ARM host/Pi. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U RegisterShellHook(U, U);
API U RegisterWindowMessageA(const char *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DestroyWindow(U);
API U IsWindow(U);
API U DefWindowProcA(U, U, U, U);
API U ShowWindow(U, int);
API U SetFocus(U);
API U GetForegroundWindow(void);
API U SetWindowTextA(U, const char *);
API U PeekMessageA(void *, U, U, U, U);
API U DispatchMessageA(void *);
API void ExitProcess(U);
static U message, delivered, reject;
static void check(int value, U code)
{
    if (!value)
        ExitProcess(code);
}
static U __stdcall procedure(U h, U m, U w, U l)
{
    if (m == 0x81 && reject == 3)
        return 0;
    if (m == 1 && reject == 1)
        return ~0u;
    if (m == 5 && reject == 2)
    {
        check(DestroyWindow(h), 1941);
        return 0;
    }
    if (m >= 0xc000)
    {
        ++delivered;
        return w ^ l;
    }
    return DefWindowProcA(h, m, w, l);
}
static U window(U ex, U style, U owner, U instance)
{
    return CreateWindowExA(ex, "NEO98ShellContract", "Shell event source", style, 30, 40, 260, 160, owner, 0,
                           instance, 0);
}
static void expect(U hwnd, U msg, U wp, U lp)
{
    U value[7] = {};
    check(PeekMessageA(value, hwnd, msg, msg, 1), 1900);
    check(value[0] == hwnd && value[1] == msg && value[2] == wp && value[3] == lp, 1901);
    U before = delivered;
    check(DispatchMessageA(value) == (wp ^ lp) && delivered == before + 1, 1902);
}
static void empty(U hwnd)
{
    U value[7];
    check(!PeekMessageA(value, hwnd, 0xc000, 0xffff, 0), 1903);
}
void run_shell_test(U instance)
{
    struct
    {
        U style;
        U(__stdcall *proc)(U, U, U, U);
        int ce, we;
        U instance, icon, cursor, background;
        const char *menu, *name;
    } klass = {0, procedure, 0, 0, instance, 0, 0, 16, 0, "NEO98ShellContract"};
    check(RegisterClassA(&klass), 1904);
    // Exercise slot reuse across the native WORD-handle generation wrap.
    U listener = 0;
    for (U i = 0; i < 260; ++i)
    {
        listener = window(0, 0, 0, instance);
        check(listener && listener <= 65535 && DestroyWindow(listener) && !IsWindow(listener), 1905);
    }
    U destroyed_handle = listener;
    listener = window(0, 0, 0, instance);
    check(listener && listener <= 65535 && listener != destroyed_handle && !IsWindow(destroyed_handle), 1906);
    message = RegisterWindowMessageA("SHELLHOOK");
    check(message >= 0xc000 && RegisterWindowMessageA("SHELLHOOK") == message, 1907);
    check(RegisterShellHook(0, 4) == 1 && RegisterShellHook(listener, 1), 1908);
    U target = window(0, 0x10c00000, 0, instance);
    check(target, 1909);
    expect(listener, message, 1, target);
    ShowWindow(target, 1);
    expect(listener, message, 4, target);
    check(GetForegroundWindow() == target, 1910);
    ShowWindow(target, 1);
    empty(listener);
    // Showing these windows must not implicitly activate them: this part
    // checks shell visibility, then tests activation explicitly with SetFocus.
    U child = window(0, 0x50000000, target, instance), owned = window(0, 0, target, instance),
      tool = window(0x80, 0, 0, instance);
    check(child && owned && tool, 1911);
    ShowWindow(owned, 8);
    ShowWindow(tool, 8);
    empty(listener);
    SetFocus(child);
    empty(listener);
    check(GetForegroundWindow() == target, 1912);
    SetFocus(owned);
    // Focus now uses the activation path; owned popups are not independent native shell candidates.
    empty(listener);
    SetFocus(target);
    expect(listener, message, 4, target);
    check(SetWindowTextA(target, "Renamed by unchanged x86 code"), 1913);
    expect(listener, message, 6, target);
    ShowWindow(target, 0);
    expect(listener, message, 2, target);
    check(GetForegroundWindow()!=target, 1940); // Hiding can activate another eligible window.
    ShowWindow(target, 1);
    expect(listener, message, 1, target);
    expect(listener, message, 4, target);
    empty(listener);
    check(DestroyWindow(target) && !IsWindow(child) && !IsWindow(owned) && DestroyWindow(tool), 1914);
    expect(listener, message, 2, target);
    empty(listener);
    target = window(0, 0, 0, instance);
    empty(listener);
    ShowWindow(target, 1);
    expect(listener, message, 1, target);
    expect(listener, message, 4, target);
    ShowWindow(target, 0);
    expect(listener, message, 2, target);
    check(DestroyWindow(target), 1915);
    empty(listener);
    // WS_EX_APPWINDOW (40000h) puts an owned top-level window on the
    // taskbar; without it the owned window above stays off.
    {
        U owner = window(0, 0, 0, instance), app = window(0x40000, 0, owner, instance);
        check(owner && app, 1944);
        ShowWindow(app, 8);
        expect(listener, message, 1, app);
        empty(listener);
        check(DestroyWindow(app), 1945);
        expect(listener, message, 2, app);
        check(DestroyWindow(owner), 1946);
        empty(listener);
    }
    reject = 1;
    check(!window(0, 0x10000000, 0, instance), 1916);
    reject = 2;
    // Popups receive WM_SIZE during creation; overlapped windows defer it until show.
    check(!window(0, 0x90000000, 0, instance), 1942);
    reject = 3;
    check(!window(0, 0x10000000, 0, instance), 1943);
    reject = 0;
    empty(listener);
    // The original permits duplicate subscribers and removes only one match.
    check(RegisterShellHook(listener, 1) == 0xfffffffcu, 1917);
    target = window(0, 0, 0, instance);
    ShowWindow(target, 8);
    expect(listener, message, 1, target);
    expect(listener, message, 1, target);
    check(RegisterShellHook(listener, 0), 1918);
    check(DestroyWindow(target), 1919);
    expect(listener, message, 2, target);
    empty(listener);
    check(RegisterShellHook(listener, 0) && !RegisterShellHook(listener, 0), 1920);
    for (U i = 0; i < 10; ++i)
        check(RegisterShellHook(listener, 1), 1921);
    check(!RegisterShellHook(listener, 1), 1922);
    target = window(0, 0, 0, instance);
    ShowWindow(target, 8);
    for (U i = 0; i < 10; ++i)
        expect(listener, message, 1, target);
    check(DestroyWindow(target), 1923);
    for (U i = 0; i < 10; ++i)
        expect(listener, message, 2, target);
    for (U i = 0; i < 10; ++i)
        check(RegisterShellHook(listener, 0), 1924);
    empty(listener);
    // Flag-3 recipients acknowledge events; saturation queues an extra message.
    check(RegisterShellHook(listener, 3) && !RegisterShellHook(0, 4), 1925);
    target = window(0, 0, 0, instance);
    ShowWindow(target, 8);
    expect(listener, message, 1, target);
    check(RegisterShellHook(listener, 5), 1926);
    for (U i = 0; i < 55; ++i)
        check(SetWindowTextA(target, "Taskman update"), 1927);
    for (U i = 0; i < 50; ++i)
    {
        expect(listener, message, 6, target);
        check(RegisterShellHook(listener, 5), 1928);
    }
    expect(listener, message, 0x42a, ~0u);
    empty(listener);
    check(RegisterShellHook(listener, 5) && SetWindowTextA(target, "After rescan"), 1929);
    expect(listener, message, 6, target);
    check(RegisterShellHook(listener, 5), 1930);
    check(DestroyWindow(target), 1931);
    expect(listener, message, 2, target);
    check(RegisterShellHook(listener, 5) && RegisterShellHook(listener, 0) && RegisterShellHook(0, 4), 1932);
    // The special shell uses separate legacy messages and argument placement.
    check(RegisterShellHook(listener, 2), 1933);
    U created = RegisterWindowMessageA("OTHERWINDOWCREATED"),
      destroyed = RegisterWindowMessageA("OTHERWINDOWDESTROYED");
    target = window(0, 0, 0, instance);
    ShowWindow(target, 8);
    expect(listener, created, target, 0);
    SetFocus(target);
    check(SetWindowTextA(target, "Legacy listener"), 1934);
    empty(listener);
    check(DestroyWindow(target), 1935);
    expect(listener, destroyed, target, 0);
    check(RegisterShellHook(listener, 0) && DestroyWindow(listener), 1936);
    // Invalid subscribers are pruned on an actual later window event.
    listener = window(0, 0, 0, instance);
    check(RegisterShellHook(listener, 1) && DestroyWindow(listener), 1937);
    target = window(0, 0, 0, instance);
    ShowWindow(target, 8);
    check(target && !RegisterShellHook(listener, 0), 1938);
    check(DestroyWindow(target) && delivered > 80, 1939);
}
