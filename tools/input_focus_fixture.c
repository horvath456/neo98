/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
typedef struct
{
    U hwnd, message, wp, lp, time;
    int x, y;
} Message;
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DestroyWindow(U);
API U IsWindow(U);
API U DefWindowProcA(U, U, U, U);
API U SetFocus(U);
API U GetFocus(void);
API U GetActiveWindow(void);
API U SetForegroundWindow(U);
API U GetKeyState(U);
API U GetAsyncKeyState(U);
API U SetKeyboardState(const unsigned char *);
API U SetWindowTextA(U, const char *);
API U SetWindowLongA(U, int, U);
API U GetMessageA(Message *, U, U, U);
API U PeekMessageA(Message *, U, U, U, U);
API U PostMessageA(U, U, U, U);
API U TranslateMessage(const Message *);
API U CreateThread(U, U, U, U, U, U);
API U GetCurrentThreadId(void);
API U WaitForSingleObject(U, U);
API U CloseHandle(U);
API void ExitProcess(U);
static U instance, root, first, target, third, replacement, second_root, second_child;
static U armed, mode, kills, stale, mouse_activation, main_thread;
static volatile U worker_thread;
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static U __stdcall worker(U unused)
{
    (void)unused;
    worker_thread = GetCurrentThreadId();
    return 98;
}
static U __stdcall procedure(U, U, U, U);
static U button(void)
{
    U h = CreateWindowExA(0, "BUTTON", "Input target", 0x50000000, 160, 20, 110, 40, root, 98, instance, 0);
    check(h != 0, 2500);
    SetWindowLongA(h, -4, (U)procedure);
    return h;
}
static U __stdcall procedure(U h, U m, U w, U l)
{
    if (m == 7 && armed && h == target)
        ++stale;
    if (m == 6 && h == second_root && (w & 65535))
        mouse_activation = w & 65535;
    if (m == 8 && h == first && armed)
    {
        armed = 0;
        ++kills;
        check(GetCurrentThreadId() == main_thread && GetFocus() == target, 2501);
        check(!GetKeyState('Q') && !GetKeyState(1), 2502);
        check(GetAsyncKeyState('Q') == 1 && !GetAsyncKeyState('Q'), 2503);
        // This wait has to suspend a real application fiber, including the USER call stack.
        worker_thread = 0;
        U thread = CreateThread(0, 0, (U)worker, 0, 0, 0);
        check(thread && WaitForSingleObject(thread, ~0u) == 0, 2504);
        check(worker_thread && worker_thread != main_thread && GetCurrentThreadId() == main_thread, 2505);
        check(CloseHandle(thread), 2506);
        if (mode == 1)
        {
            SetFocus(third);
            // A nested message pump can finish the button release and route later keys.
            // Messages posted by this callback must not inherit hardware-state metadata.
            Message msg;
            check(PostMessageA(root, 0x8002, 0, 0) && PeekMessageA(&msg, root, 0x8002, 0x8002, 1), 2507);
            check(!GetKeyState('Q') && !GetKeyState(1), 2508);
            check(PostMessageA(root, 0x100, 'Z', 1) && PeekMessageA(&msg, root, 0x100, 0x101, 1), 2509);
            check(msg.wp == 'Z' && !GetKeyState('Q') && !GetKeyState(1) && TranslateMessage(&msg) == 1, 2510);
        }
        else
        {
            check(DestroyWindow(target), 2511);
            replacement = button();
            check(replacement != target && (replacement & 255) == (target & 255) && !IsWindow(target) &&
                      GetFocus() == root,
                  2512);
        }
    }
    if (m == 7 && h == target && (mode == 1 || mode == 2))
        ++stale;
    return DefWindowProcA(h, m, w, l);
}
static void clear_messages(void)
{
    Message msg;
    while (PeekMessageA(&msg, 0, 0, 0, 1))
        DefWindowProcA(msg.hwnd, msg.message, msg.wp, msg.lp);
    unsigned char keys[256];
    for (U i = 0; i < 256; ++i)
        keys[i] = 0;
    check(SetKeyboardState(keys), 2513);
}
static void wake(const char *title)
{
    Message msg;
    SetWindowTextA(root, title);
    check(GetMessageA(&msg, root, 0x8001, 0x8001), 2514);
    // Posted wake-ups precede hardware. Explicitly request the mouse input
    // before asserting the focus callbacks produced while routing it.
    PeekMessageA(&msg, 0, 0x201, 0x202, 0);
}
static void keys(U h, U key)
{
    Message msg;
    check(PeekMessageA(&msg, h, 0x100, 0x101, 1) && msg.message == 0x100 && msg.wp == key, 2515);
    check(GetKeyState(key) == 0xffffff80 && !GetKeyState(1), 2516);
    check(PeekMessageA(&msg, h, 0x100, 0x101, 1) && msg.message == 0x101 && msg.wp == key, 2517);
    check(!GetKeyState(key), 2518);
}
void run_input_focus_test(U module)
{
    instance = module;
    main_thread = GetCurrentThreadId();
    U klass[] = {0, (U)procedure, 0, 0, module, 0, 0, 16, 0, (U) "NEO98MouseFocusContract"};
    check(RegisterClassA(klass), 2519);
    root = CreateWindowExA(0, "NEO98MouseFocusContract", "Mouse focus setup", 0x10cf0000, 40, 40, 520, 260, 0,
                           0, module, 0);
    first = CreateWindowExA(0, "NEO98MouseFocusContract", "Previous focus", 0x50000000, 10, 20, 110, 40, root,
                            0, module, 0);
    third = CreateWindowExA(0, "NEO98MouseFocusContract", "Redirected focus", 0x50000000, 300, 20, 110, 40,
                            root, 0, module, 0);
    target = button();
    check(root && first && third, 2520);
    SetFocus(first);
    clear_messages();
    mode = armed = 1;
    wake("Mouse focus contract 1");
    check(kills == 1 && !stale && GetFocus() == third && GetActiveWindow() == root, 2521);
    keys(third, 'Q');
    // Button snapshots were captured before Q, even though Q was physically received already.
    Message msg;
    check(PeekMessageA(&msg, target, 0x201, 0x202, 1) && msg.message == 0x201, 2522);
    check(GetKeyState(1) == 0xffffff80 && !GetKeyState('Q'), 2523);
    check(PeekMessageA(&msg, target, 0x201, 0x202, 1) && msg.message == 0x202 && !GetKeyState(1), 2524);
    clear_messages();
    SetFocus(first);
    mode = 2;
    armed = 1;
    wake("Mouse focus contract 2");
    check(kills == 2 && !stale && !IsWindow(target) && IsWindow(replacement) && GetFocus() == root, 2525);
    check(!PeekMessageA(&msg, replacement, 0x201, 0x201, 1), 2526);
    keys(root, 'Q');
    clear_messages();
    mode = 3;
    second_root = CreateWindowExA(0, "NEO98MouseFocusContract", "Mouse activation target", 0x10cf0000, 580,
                                  320, 200, 120, 0, 0, module, 0);
    second_child = CreateWindowExA(0, "NEO98MouseFocusContract", "Second root child", 0x50000000, 10, 10, 110,
                                   40, second_root, 0, module, 0);
    check(second_root && second_child, 2528);
    check(SetForegroundWindow(root), 2532);
    wake("Mouse focus contract 3");
    check(mouse_activation == 2 && GetFocus() == second_child && GetActiveWindow() == second_root, 2529);
    keys(second_child, 'S');
    clear_messages();
    check(DestroyWindow(root) && DestroyWindow(second_root), 2530);
    check(!GetFocus() && !GetActiveWindow(), 2531);
}
