/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest fixture for USER32!RegisterHotKey, USER32!UnregisterHotKey and
   GDI32!GetClipBox. The runner injects real key packets once the three hot
   keys are live; every WM_HOTKEY here arrives through the native input queue. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U RegisterClassA(U *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U DestroyWindow(U);
API U ShowWindow(U, U);
API U UpdateWindow(U);
API U PeekMessageA(U *, U, U, U, U);
API U RegisterHotKey(U, U, U, U);
API U UnregisterHotKey(U, U);
API U GetDC(U);
API U ReleaseDC(U, U);
API U GetClientRect(U, int *);
API U GetClipBox(U, int *);
API U IntersectClipRect(U, int, int, int, int);
API U SaveDC(U);
API U RestoreDC(U, int);
API U GetCurrentThreadId(void);
API void Sleep(U);
API void ExitProcess(U);

#define MOD_ALT 1
#define MOD_CONTROL 2
#define MOD_SHIFT 4
#define MOD_WIN 8
#define WM_HOTKEY 0x312
#define ERRORREGION 0
#define NULLREGION 1
#define SIMPLEREGION 2

static U window, thread;
static U hotkeys, keydowns;
static U seen[8], seen_wparam[8], seen_lparam[8], seen_hwnd[8];

static void check(int ok, U code)
{
    if (!ok) ExitProcess(code);
}
static U __stdcall Procedure(U hwnd, U message, U wp, U lp)
{
    return DefWindowProcA(hwnd, message, wp, lp);
}
static void note(U hwnd, U message, U wp, U lp)
{
    if (message == WM_HOTKEY && hotkeys < 8) {
        seen[hotkeys] = message;
        seen_wparam[hotkeys] = wp;
        seen_lparam[hotkeys] = lp;
        seen_hwnd[hotkeys] = hwnd;
        ++hotkeys;
    }
    /* A swallowed keystroke never reaches the window. The modifier keys
       themselves are routed normally; only the hot key's own virtual key is
       consumed at USER. */
    if ((message == 0x100 || message == 0x104) && (wp == 0x51 || wp == 0x52 || wp == 0x54))
        ++keydowns;
}
/* Spin until the expected number of hot keys has arrived or the budget runs
   out; Sleep(0) yields to the runner so it can inject the key packets. */
static void pump(U rounds, U expected)
{
    U message[7];
    U i;
    for (i = 0; i < rounds && hotkeys < expected; ++i) {
        while (PeekMessageA(message, 0, 0, 0, 1))
            note(message[0], message[1], message[2], message[3]);
        Sleep(0);
    }
    for (i = 0; i < 64; ++i) {
        while (PeekMessageA(message, 0, 0, 0, 1))
            note(message[0], message[1], message[2], message[3]);
        Sleep(0);
    }
}
static void clipbox(void)
{
    int box[4], client[4];
    U dc, saved;
    /* A DC handle that no object backs fails the gate at GDI. */
    check(GetClipBox(0, box) == ERRORREGION, 0x21);
    dc = GetDC(window);
    check(dc != 0, 0x22);
    /* GDI32 pushes a null LPRECT unchanged; the adapter answers ERROR rather
       than faulting in the 16-bit probe. */
    check(GetClipBox(dc, 0) == ERRORREGION, 0x23);
    check(GetClientRect(window, client), 0x24);
    box[0] = box[1] = box[2] = box[3] = -1;
    check(GetClipBox(dc, box) == SIMPLEREGION, 0x25);
    check(box[0] == client[0] && box[1] == client[1], 0x26);
    check(box[2] == client[2] && box[3] == client[3], 0x27);
    saved = SaveDC(dc);
    check(saved != 0, 0x28);
    check(IntersectClipRect(dc, 4, 6, 20, 30) != ERRORREGION, 0x29);
    box[0] = box[1] = box[2] = box[3] = -1;
    check(GetClipBox(dc, box) == SIMPLEREGION, 0x2a);
    check(box[0] == 4 && box[1] == 6 && box[2] == 20 && box[3] == 30, 0x2b);
    /* An empty clip is the NULLREGION the worker returns at GDI without
       converting the rectangle. */
    check(IntersectClipRect(dc, 100, 100, 100, 100) == NULLREGION, 0x2c);
    check(GetClipBox(dc, box) == NULLREGION, 0x2d);
    check(RestoreDC(dc, -1), 0x2e);
    check(GetClipBox(dc, box) == SIMPLEREGION, 0x2f);
    check(ReleaseDC(window, dc), 0x30);
}
void mainCRTStartup(void)
{
    U class_fields[10];
    U i;
    int lparam;
    thread = GetCurrentThreadId();
    for (i = 0; i < 10; ++i) class_fields[i] = 0;
    class_fields[1] = (U)(void *)Procedure;
    class_fields[9] = (U) "HotKeyFixture";
    check(RegisterClassA(class_fields), 0x10);
    window = CreateWindowExA(0, "HotKeyFixture", "Hot keys", 0x10cf0000,
                             10, 10, 200, 120, 0, 0, 0, 0);
    check(window != 0, 0x11);
    ShowWindow(window, 5);
    UpdateWindow(window);

    clipbox();

    /* USER accepts the first registration of a combination. */
    check(RegisterHotKey(window, 100, MOD_CONTROL, 0x1b), 0x12);
    check(RegisterHotKey(window, 101, MOD_WIN | MOD_SHIFT, 0x52), 0x13);
    /* the combination is already in the global list, whoever owns it. */
    check(!RegisterHotKey(window, 102, MOD_CONTROL, 0x1b), 0x14);
    /* The same record re-registered unchanged is a duplicate of itself. */
    check(!RegisterHotKey(window, 100, MOD_CONTROL, 0x1b), 0x15);
    /* replaces the combination of an existing (window, id) in place. */
    check(RegisterHotKey(window, 100, MOD_ALT, 0x51), 0x16);
    /* a modifier bit above 0Fh never reaches the body. */
    check(!RegisterHotKey(window, 103, 0x10, 0x5a), 0x17);
    /* a window handle that does not resolve is refused. */
    check(!RegisterHotKey(0xdeadbeef, 104, MOD_ALT, 0x59), 0x18);
    /* A null window is a thread hot key: no ownership test, no window. */
    check(RegisterHotKey(0, 105, MOD_CONTROL | MOD_ALT, 0x54), 0x19);

    pump(20000, 3);

    check(hotkeys == 3, 0x40 + hotkeys);
    /* Alt+Q, the replaced combination of id 100, on the window. */
    check(seen_hwnd[0] == window, 0x50);
    check(seen_wparam[0] == 100, 0x51);
    lparam = (int)(MOD_ALT | (0x51 << 16));
    check(seen_lparam[0] == (U)lparam, 0x52);
    /* Win+Shift+R on the window. */
    check(seen_hwnd[1] == window, 0x53);
    check(seen_wparam[1] == 101, 0x54);
    lparam = (int)((MOD_WIN | MOD_SHIFT) | (0x52 << 16));
    check(seen_lparam[1] == (U)lparam, 0x55);
    /* Ctrl+Alt+T is a thread hot key, so it arrives with window handle 0. */
    check(seen_hwnd[2] == 0, 0x56);
    check(seen_wparam[2] == 105, 0x57);
    lparam = (int)((MOD_CONTROL | MOD_ALT) | (0x54 << 16));
    check(seen_lparam[2] == (U)lparam, 0x58);
    /* Every hot key swallowed its own keystroke. */
    check(keydowns == 0, 0x59);

    check(UnregisterHotKey(window, 101), 0x1a);
    check(!UnregisterHotKey(window, 101), 0x1b);
    check(!UnregisterHotKey(window, 0x777), 0x1c);

    pump(2000, 4);
    /* The unregistered combination no longer produces anything. */
    check(hotkeys == 3, 0x60 + hotkeys);

    check(UnregisterHotKey(window, 100), 0x1d);
    check(UnregisterHotKey(0, 105), 0x1e);
    check(DestroyWindow(window), 0x1f);
    ExitProcess(0);
}
