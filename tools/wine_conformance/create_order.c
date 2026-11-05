/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static unsigned mode, recording, used, minmax_count;
static char events[1024];
static void event(unsigned code) { if (recording && used + 16 < sizeof events) used += sprintf(events + used, "%x,", code); }
static LRESULT CALLBACK hook(int code, WPARAM w, LPARAM l)
{
    if (recording && code == HCBT_CREATEWND) {
        CBT_CREATEWNDA *data = (CBT_CREATEWNDA *)l;
        event(0x10003);
        if (mode == 1) { data->lpcs->x = 30; data->lpcs->y = 40; data->lpcs->cx = 70; data->lpcs->cy = 60; }
        if (mode == 2) return 1;
    }
    return CallNextHookEx(NULL, code, w, l);
}
static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_GETMINMAXINFO || m == WM_NCCREATE || m == WM_NCCALCSIZE || m == WM_CREATE ||
        m == WM_SIZE || m == WM_MOVE || m == WM_NCDESTROY) event(m);
    if (recording && m == WM_GETMINMAXINFO) {
        MINMAXINFO *info = (MINMAXINFO *)l;
        ++minmax_count;
        if (mode == 1) { info->ptMinTrackSize.x = 80; info->ptMinTrackSize.y = 70; }
    }
    return DefWindowProcA(h, m, w, l);
}
START_TEST(create_order)
{
    WNDCLASSA cls;
    HWND parent, h;
    HHOOK handle;
    unsigned kind;
    static const DWORD styles[] = {WS_OVERLAPPEDWINDOW, WS_CHILD, WS_CHILD | WS_THICKFRAME, WS_POPUP, WS_POPUP | WS_THICKFRAME};
    memset(&cls, 0, sizeof cls);
    cls.lpfnWndProc = proc; cls.hInstance = GetModuleHandleA(NULL); cls.lpszClassName = "CreateOrder";
    ok(RegisterClassA(&cls) != 0, "class\n");
    parent = CreateWindowA(cls.lpszClassName, "parent", WS_OVERLAPPEDWINDOW, 20, 20, 300, 240, NULL, NULL, cls.hInstance, NULL);
    ok(parent != NULL, "parent\n");
    handle = SetWindowsHookExA(WH_CBT, hook, NULL, GetCurrentThreadId());
    ok(handle != NULL, "hook\n");
    for (kind = 0; kind < sizeof styles / sizeof styles[0]; ++kind) for (mode = 0; mode < 3; ++mode) {
        RECT rect = {0,0,0,0}; POINT origin;
        used = minmax_count = 0; events[0] = 0; recording = 1;
        h = CreateWindowA(cls.lpszClassName, "test", styles[kind], 10, 15, 150, 120,
                          (styles[kind] & WS_CHILD) ? parent : NULL, NULL, cls.hInstance, NULL);
        recording = 0;
        ok(mode == 2 ? h == NULL : h != NULL, "create %u/%u\n", kind, mode);
        if (h) {
            GetWindowRect(h, &rect);
            if (styles[kind] & WS_CHILD) {
                origin.x = rect.left; origin.y = rect.top; ScreenToClient(parent, &origin);
                OffsetRect(&rect, origin.x - rect.left, origin.y - rect.top);
            }
        }
        trace("CREATE_ORDER kind=%u mode=%u created=%u minmax=%u rect=%ld,%ld,%ld,%ld events=%s\n",
              kind, mode, h != NULL, minmax_count, rect.left, rect.top, rect.right, rect.bottom, events);
        if (h) DestroyWindow(h);
    }
    UnhookWindowsHookEx(handle); DestroyWindow(parent);
}
