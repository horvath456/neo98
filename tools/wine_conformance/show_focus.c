/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"

/* Command order and window style from Wine 1.0 user32/tests/msg.c,
 * test_ShowWindow. Record queue state in addition to the original messages. */
static HWND observed;
static HHOOK hook;
static unsigned phase;
static unsigned role(HWND h) { return !h ? 0 : h == observed ? 1 : 2; }
static LRESULT CALLBACK observe(int code, WPARAM wp, LPARAM lp)
{
    if (phase && code == HCBT_ACTIVATE)
        trace("SHOW_FOCUS_ACTIVATE phase=%u target=%u active=%u foreground=%u\n",
              phase, role((HWND)wp), role(GetActiveWindow()), role(GetForegroundWindow()));
    if (phase && code == HCBT_SETFOCUS)
        trace("SHOW_FOCUS_HOOK phase=%u new=%u old=%u current=%u\n",
              phase, role((HWND)wp), role((HWND)lp), role(GetFocus()));
    return CallNextHookEx(hook, code, wp, lp);
}
static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (phase && h == observed && m == WM_ACTIVATE)
        trace("SHOW_FOCUS_ACTIVE phase=%u wparam=%x other=%u focus=%u active=%u foreground=%u\n",
              phase, (unsigned)w, role((HWND)l), role(GetFocus()), role(GetActiveWindow()), role(GetForegroundWindow()));
    if (phase && h == observed && (m == WM_WINDOWPOSCHANGING || m == WM_WINDOWPOSCHANGED))
        trace("SHOW_FOCUS_POS phase=%u message=%x flags=%x\n", phase, m, ((WINDOWPOS *)l)->flags);
    return DefWindowProcA(h, m, w, l);
}
static void flush(void)
{
    MSG msg;
    int remaining = 200, minimum = 50;
    DWORD deadline = GetTickCount() + remaining;
    /* Same bounded message wait as Wine's flush_events: allow the shell's
     * input queue to run before comparing the next stable state. */
    while (remaining > 0) {
        if (MsgWaitForMultipleObjects(0, NULL, FALSE, minimum, QS_ALLINPUT) == WAIT_TIMEOUT) break;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
        remaining = (LONG)(deadline - GetTickCount());
        minimum = 10;
    }
}
START_TEST(show_focus)
{
    static const unsigned commands[] = {
        1,1,0,0,2,2,0,0,3,3,0,0,4,4,0,0,5,5,6,6,0,7,7,0,0,8,8,0,0,
        9,9,0,0,0xcc,0xcc,0,9,9,4,6,6,3,3,6,6,9,9,3,5,1,1,0,0,6,0,4,5
    };
    unsigned i;
    WNDCLASSA cls = {0};
    cls.hInstance = GetModuleHandleA(NULL); cls.lpfnWndProc = proc;
    cls.lpszClassName = "ShowFocus98";
    ok(RegisterClassA(&cls) != 0, "register\n");
    observed = CreateWindowA(cls.lpszClassName, "show focus",
        WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_POPUP|WS_CLIPSIBLINGS,
        120,120,90,90,NULL,NULL,cls.hInstance,NULL);
    ok(observed != NULL, "window\n");
    if (!observed) return;
    hook = SetWindowsHookExA(WH_CBT, observe, NULL, GetCurrentThreadId());
    ok(hook != NULL, "hook\n");
    flush();
    for (i = 0; i < sizeof(commands)/sizeof(commands[0]); ++i) {
        unsigned before_focus = role(GetFocus()), before_active = role(GetActiveWindow());
        unsigned before_foreground = role(GetForegroundWindow());
        BOOL result;
        phase = i + 1;
        result = ShowWindow(observed, commands[i]);
        trace("SHOW_FOCUS_STATE phase=%u cmd=%u result=%u before=%u/%u/%u after=%u/%u/%u\n",
              phase, commands[i], !!result, before_focus, before_active, before_foreground,
              role(GetFocus()), role(GetActiveWindow()), role(GetForegroundWindow()));
        flush();
        trace("SHOW_FOCUS_IDLE phase=%u state=%u/%u/%u\n", phase,
              role(GetFocus()), role(GetActiveWindow()), role(GetForegroundWindow()));
    }
    phase = 0;
    UnhookWindowsHookEx(hook); DestroyWindow(observed);
    UnregisterClassA(cls.lpszClassName, cls.hInstance);
}
