/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"

static HHOOK hook;
static unsigned foreign_activations, own_activations;
static HWND observed;
static unsigned phase;
static LRESULT CALLBACK observe(int code, WPARAM wp, LPARAM lp)
{
    if (code == HCBT_SETFOCUS)
        trace("CBT_HIDE_FOCUS phase=%u new=%u old=%u\n", phase,
              !wp ? 0 : (HWND)wp == observed ? 1 : 2,
              !lp ? 0 : (HWND)lp == observed ? 1 : 2);
    if (code == HCBT_ACTIVATE) {
        DWORD process = 0;
        GetWindowThreadProcessId((HWND)wp, &process);
        if (process != GetCurrentProcessId()) ++foreign_activations;
        else ++own_activations;
    }
    return CallNextHookEx(hook, code, wp, lp);
}

START_TEST(cbt_activation)
{
    HWND window;
    unsigned i;
    const int commands[] = {SW_HIDE, SW_MINIMIZE};
    WNDCLASSA cls = {0};
    cls.lpfnWndProc = DefWindowProcA;
    cls.hInstance = GetModuleHandleA(NULL);
    cls.lpszClassName = "Neo98CbtActivation";
    ok(RegisterClassA(&cls) != 0, "register class\n");
    window = CreateWindowA(cls.lpszClassName, "CBT activation", WS_OVERLAPPEDWINDOW,
                           120, 120, 200, 140, NULL, NULL, cls.hInstance, NULL);
    ok(window != NULL, "create window\n");
    if (!window) return;
    observed = window;
    hook = SetWindowsHookExA(WH_CBT, observe, NULL, GetCurrentThreadId());
    ok(hook != NULL, "install local CBT hook\n");
    for (i = 0; i < 2; ++i) {
        MSG msg;
        ShowWindow(window, SW_SHOWNORMAL);
        SetForegroundWindow(window);
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
        ok(GetForegroundWindow() == window, "phase %u own foreground\n", i);
        foreign_activations = own_activations = 0;
        phase = i + 1;
        trace("CBT_HIDE_STATE phase=%u before=%u\n", phase, GetFocus() == window);
        ShowWindow(window, commands[i]);
        trace("CBT_HIDE_STATE phase=%u after=%u\n", phase, GetFocus() == window);
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
        trace("CBT_ACTIVATION phase=%u own=%u foreign=%u\n", i,
              own_activations, foreign_activations);
        ok(foreign_activations == 0, "phase %u local hook saw %u foreign activations\n",
           i, foreign_activations);
        phase = 0;
    }
    UnhookWindowsHookEx(hook);
    DestroyWindow(window);
    UnregisterClassA(cls.lpszClassName, cls.hInstance);
}
