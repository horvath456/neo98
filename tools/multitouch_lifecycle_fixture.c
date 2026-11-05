/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "../sdk/lib/neo98_touch.h"
static NEO98_TOUCH touch;
static HWND main_window, other;
static unsigned stage, contact_id, cancels, legacy_down, legacy_up, main_down, main_up;
static int switch_focus, closed;
static void check(int ok, unsigned code) { if (!ok) ExitProcess(code); }
static void contact(void *context, const NEO98_TOUCH_EVENT *e)
{
    (void)context;
    if (e->phase == NEO98_TOUCH_CANCEL_ALL) { ++cancels; return; }
    if (stage == 0) { check(e->phase == 1 && e->x == 30 && e->y == 40, 10); contact_id = e->id; }
    else if (stage == 1) check(e->phase == 2 && e->id == contact_id && e->x == 460 && e->y == 460, 11);
    else if (stage == 2) check(e->phase == 3 && e->id == contact_id && e->x == 460 && e->y == 460, 12);
    else if (stage == 3) { check(e->phase == 1, 13); switch_focus = 1; }
    else if (stage == 4) { check(e->phase == 1, 15); contact_id = e->id; }
    else if (stage == 5) check(e->phase == 3 && e->id == contact_id, 16);
    else ExitProcess(14);
    ++stage;
}
static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (window == main_window && !closed) {
        if (Neo98TouchHandle(&touch, message, wp, lp, contact, 0)) {
            if (switch_focus) { switch_focus = 0; SetForegroundWindow(other); }
            return 0;
        }
    }
    if (message == WM_LBUTTONDOWN) { if (window == other) ++legacy_down; else ++main_down; }
    if (message == WM_LBUTTONUP) { if (window == other) ++legacy_up; else ++main_up; }
    if (message == WM_KEYDOWN && wp == 'U') { Neo98TouchClose(&touch); closed = 1; }
    if (message == WM_KEYDOWN && wp == 'A') {
        check(stage == 6, 200 + stage);
        check(cancels, 300);
        check(legacy_down == 1, 310 + legacy_down);
        check(legacy_up == 1, 320 + legacy_up);
        check(closed && main_down == 1 && main_up == 1, 21);
        ExitProcess(0);
    }
    return DefWindowProcA(window, message, wp, lp);
}
void mainCRTStartup(void)
{
    WNDCLASSA klass = {0};
    MSG message;
    klass.hInstance = GetModuleHandleA(0); klass.lpfnWndProc = procedure; klass.lpszClassName = "TouchLifecycleFixture";
    check(RegisterClassA(&klass), 1);
    main_window = CreateWindowExA(0, klass.lpszClassName, "Touch main", WS_POPUP | WS_VISIBLE,
                                  40, 40, 320, 240, 0, 0, klass.hInstance, 0);
    other = CreateWindowExA(0, klass.lpszClassName, "Legacy mouse", WS_POPUP,
                            400, 40, 200, 240, 0, 0, klass.hInstance, 0);
    ShowWindow(other, 8); ShowWindow(main_window, 1);
    check(Neo98TouchOpen(&touch, main_window, WM_APP + 42), 2);
    while (GetMessageA(&message, 0, 0, 0) > 0) DispatchMessageA(&message);
    ExitProcess(80);
}
