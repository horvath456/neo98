/* SPDX-License-Identifier: GPL-2.0-or-later
 * Observe the real SHELLHOOK messages from borderless fullscreen changes. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U GetModuleHandleA(const char *);
API void ExitProcess(U);
API U RegisterShellHook(U, U);
API U RegisterWindowMessageA(const char *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U GetSystemMetrics(U);
API U SetWindowPos(U, U, int, int, int, int, U);
API U SetForegroundWindow(U);
API U GetForegroundWindow(void);
API U ShowWindow(U, U);
API U PeekMessageA(void *, U, U, U, U);
static U listener, message;
static void check(int ok, U code) { if (!ok) ExitProcess(code); }
static void expect(U event, U target, U code)
{
    U msg[7];
    check(PeekMessageA(msg, listener, message, message, 1), code);
    check(msg[2] == event && msg[3] == target, code + 1);
}
static void drain(void)
{
    U msg[7];
    while (PeekMessageA(msg, listener, message, message, 1)) {}
}
static void empty(U code)
{
    U msg[7];
    check(!PeekMessageA(msg, listener, message, message, 0), code);
}
void mainCRTStartup(void)
{
    U instance = GetModuleHandleA(0);
    struct { U style, proc, ce, we, instance, icon, cursor, brush, menu; const char *name; }
        klass = {0, (U)DefWindowProcA, 0, 0, instance, 0, 0, 16, 0, "FullscreenShellTest"};
    check(RegisterClassA(&klass), 1);
    listener = CreateWindowExA(0, klass.name, "Listener", 0, 0, 0, 20, 20, 0, 0, instance, 0);
    message = RegisterWindowMessageA("SHELLHOOK");
    check(listener && message && RegisterShellHook(0, 4) && RegisterShellHook(listener, 1), 2);
    U window = CreateWindowExA(0, klass.name, "Game", 0x90000000, 20, 20, 300, 200, 0, 0, instance, 0);
    ShowWindow(window, 1);
    check(SetForegroundWindow(window), 3);
    drain();
    int width = GetSystemMetrics(0), height = GetSystemMetrics(1);
    check(SetWindowPos(window, 0, 0, 0, width, height, 0x20), 4);
    expect(0x8004, window, 10);
    drain();
    // Repeating the same geometry produces no new fullscreen transition.
    check(SetWindowPos(window, 0, 0, 0, width, height, 0x20), 5);
    empty(12);
    // An owned dialog must not bring the taskbar over its fullscreen owner.
    U dialog = CreateWindowExA(0, klass.name, "Dialog", 0x90000000, 40, 40, 100, 100, window, 0, instance, 0);
    ShowWindow(dialog, 1);
    check(SetForegroundWindow(dialog), 13);
    U owned_message[7];
    while (PeekMessageA(owned_message, listener, message, message, 1))
        check(owned_message[2] != 4, 14);
    ShowWindow(dialog, 0);
    // Hiding a popup must not publish a false ordinary activation between
    // the popup and its still-visible fullscreen owner (Half-Life WAV test).
    while (PeekMessageA(owned_message, listener, message, message, 1))
        check(owned_message[2] != 4, 16);
    check(SetForegroundWindow(window), 15);
    drain();
    check(SetWindowPos(window, 0, 20, 20, 300, 200, 0x20), 6);
    expect(4, window, 20);
    drain();
    // A full-width window ending above the taskbar remains ordinary.
    check(SetWindowPos(window, 0, 0, 0, width, height - 28, 0x20), 7);
    empty(22);
    U other = CreateWindowExA(0, klass.name, "Other", 0x90000000, 20, 20, 100, 100, 0, 0, instance, 0);
    ShowWindow(other, 1);
    check(SetForegroundWindow(other), 8);
    drain();
    // Resizing a background game must not lower the foreground's taskbar.
    check(SetWindowPos(window, 0, 0, 0, width, height, 0x30), 9);
    empty(30);
    check(SetForegroundWindow(window), 31);
    expect(0x8004, window, 32);
    drain();
    check(SetForegroundWindow(other), 34);
    expect(4, other, 35);
    drain();
    check(SetForegroundWindow(window), 37);
    expect(0x8004, window, 38);
    drain();
    ShowWindow(window, 0);
    expect(2, window, 40);
    // Hiding first clears fullscreen coverage and then hands activation on.
    U msg[7], restored = 0;
    while (PeekMessageA(msg, listener, message, message, 1))
    {
        check(msg[2] != 0x8004, 42);
        if (msg[2] == 4 && msg[3] == GetForegroundWindow()) restored = 1;
    }
    check(restored && GetForegroundWindow() != window, 43);
    ExitProcess(0);
}
