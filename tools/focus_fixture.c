/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DestroyWindow(U);
API U DefWindowProcA(U, U, U, U);
API U SetFocus(U);
API U GetFocus(void);
API U GetActiveWindow(void);
API U GetWindowLongA(U, int);
API U SetWindowLongA(U, int, U);
API U ShowWindow(U, U);
API U IsWindow(U);
API void ExitProcess(U);
static U recording, count, events[64][3], redirect, destroy_target, instance;
static void check(int yes, U code)
{
    if (!yes)
        ExitProcess(code);
}
static U __stdcall proc(U h, U m, U w, U l)
{
    if (recording && (m == 7 || m == 8))
    {
        check(count < 64, 2400);
        events[count][0] = h;
        events[count][1] = m;
        events[count++][2] = w;
        check(GetFocus() == (m == 8 ? w : h), 2401);
    }
    if (m == 8 && redirect)
    {
        U next = redirect;
        redirect = 0;
        SetFocus(next);
    }
    if (m == 8 && destroy_target)
    {
        U doomed = destroy_target;
        destroy_target = 0;
        check(DestroyWindow(doomed), 2402);
    }
    return DefWindowProcA(h, m, w, l);
}
static U make(const char *title, U parent)
{
    return CreateWindowExA(0, "NEO98FocusContract", title, parent ? 0x50000000 : 0x10cf0000, 40, 50, 250, 160,
                           parent, 0, instance, 0);
}
void run_focus_test(U module)
{
    instance = module;
    struct
    {
        U style;
        U(__stdcall *proc)(U, U, U, U);
        int ce, we;
        U instance, icon, cursor, background;
        const char *menu, *name;
    } klass = {0, proc, 0, 0, module, 0, 0, 16, 0, "NEO98FocusContract"};
    check(RegisterClassA(&klass), 2403);
    U a = make("Focus root one", 0), b = make("Focus root two", 0), first = make("First child", a),
      deep = make("Nested child", first), third = make("Other root child", b);
    check(a && b && first && deep && third, 2404);
    SetFocus(deep);
    check(GetFocus() == deep && GetActiveWindow() == a, 2405);
    recording = 1;
    count = 0;
    check(SetFocus(first) == deep && GetFocus() == first && count == 2, 2406);
    check(events[0][0] == deep && events[0][1] == 8 && events[0][2] == first, 2407);
    check(events[1][0] == first && events[1][1] == 7 && events[1][2] == deep, 2408);
    count = 0;
    check(SetFocus(first) == first && !count, 2409);
    check(SetFocus(0) == first && GetFocus() == 0 && GetActiveWindow() == a && count == 1, 2410);
    count = 0;
    check(!SetFocus(0) && !count, 2411);
    recording = 0;
    const U style = GetWindowLongA(first, -16), root_style = GetWindowLongA(a, -16);
    SetWindowLongA(first, -16, style | 0x08000000);
    check(!SetFocus(deep) && !GetFocus(), 2412);
    SetWindowLongA(first, -16, style);
    SetWindowLongA(a, -16, root_style | 0x08000000);
    check(!SetFocus(deep) && !GetFocus(), 2413);
    SetWindowLongA(a, -16, root_style | 0x20000000);
    check(!SetFocus(deep) && !GetFocus(), 2414);
    SetWindowLongA(a, -16, root_style);
    ShowWindow(a, 0);
    SetFocus(deep);
    check(GetFocus() == deep && GetActiveWindow() == a, 2415);
    ShowWindow(a, 1);
    SetFocus(first);
    recording = 1;
    count = 0;
    redirect = third;
    check(SetFocus(deep) == first && GetFocus() == third && GetActiveWindow() == b, 2416);
    for (U i = 0; i < count; ++i)
        check(!(events[i][0] == deep && events[i][1] == 7), 2417);
    recording = 0;
    SetFocus(first);
    recording = 1;
    count = 0;
    destroy_target = deep;
    // DestroyWindow hides the focused child and returns focus to its parent.
    check(SetFocus(deep) == first && !IsWindow(deep) && GetFocus() == first, 2418);
    for (U i = 0; i < count; ++i)
        check(!(events[i][0] == deep && events[i][1] == 7), 2419);
    recording = 0;
    U replacement = make("Replacement focus child", first);
    check(replacement && replacement != deep, 2420);
    SetFocus(replacement);
    check(GetFocus() == replacement, 2421);
    check(!SetFocus(deep) && GetFocus() == replacement, 2422);
    check(DestroyWindow(a) && DestroyWindow(b), 2423);
    check(!GetFocus() && !GetActiveWindow(), 2424);
}
