/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"

static HWND button;
static WNDPROC original;
static unsigned recording, used;
static char events[2048];
static void event(unsigned message, unsigned value)
{
    if (recording && used + 24 < sizeof events)
        used += sprintf(events + used, "%x:%x,", message, value);
}
static LRESULT CALLBACK hook(int code, WPARAM w, LPARAM l)
{
    if (code == HCBT_SETFOCUS) event(0x10009, w == (WPARAM)button);
    return CallNextHookEx(NULL, code, w, l);
}
static LRESULT CALLBACK parent_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m >= WM_CTLCOLORMSGBOX && m <= WM_CTLCOLORSTATIC) event(m, 1);
    if (m == WM_COMMAND) event(m, HIWORD(w));
    return DefWindowProcA(h, m, w, l);
}
static LRESULT CALLBACK button_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_SETFOCUS || m == WM_KILLFOCUS || m == WM_PAINT || m == WM_ERASEBKGND ||
        m == WM_CAPTURECHANGED || (m >= WM_CTLCOLORMSGBOX && m <= WM_CTLCOLORSTATIC)) event(m, 0);
    if (m == BM_SETSTATE) event(m, !!w);
    return CallWindowProcA(original, h, m, w, l);
}
START_TEST(button_messages)
{
    WNDCLASSA cls;
    HWND parent;
    HHOOK hhook;
    unsigned child, visible, kind, step;
    memset(&cls, 0, sizeof cls);
    cls.lpfnWndProc = parent_proc; cls.hInstance = GetModuleHandleA(NULL); cls.lpszClassName = "ButtonMessagesParent";
    ok(RegisterClassA(&cls) != 0, "class\n");
    parent = CreateWindowA(cls.lpszClassName, "parent", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           20, 20, 320, 240, NULL, NULL, cls.hInstance, NULL);
    ok(parent != NULL, "parent\n");
    hhook = SetWindowsHookExA(WH_CBT, hook, NULL, GetCurrentThreadId());
    ok(hhook != NULL, "hook\n");
    for (child = 0; child < 2; ++child) for (visible = 0; visible < 2; ++visible) for (kind = 0; kind <= 11; ++kind) {
        /* Original Win98 hangs on the mouse-up of a parentless auto-radio
         * button, with and without WS_GROUP (retained v1/v2 reference logs).
         * Exercise automatic radio clicks in an actual parent group below. */
        if (!child && kind == BS_AUTORADIOBUTTON) continue;
        /* Bound the automatic radio-button group even for a popup. */
        button = CreateWindowA("BUTTON", "button", kind | WS_GROUP | (child ? WS_CHILD : WS_POPUP) | (visible ? WS_VISIBLE : 0),
                               10, 10, 100, 40, child ? parent : NULL, (HMENU)(child ? 10 : 0), cls.hInstance, NULL);
        ok(button != NULL, "button %u/%u/%u\n", child, visible, kind);
        original = (WNDPROC)SetWindowLongA(button, GWL_WNDPROC, (LONG)button_proc);
        UpdateWindow(button); SetFocus(NULL);
        for (step = 0; step < 6; ++step) {
            unsigned result = 0;
            used = 0; events[0] = 0; recording = 1;
            switch (step) {
            case 0: result = SendMessageA(button, WM_GETDLGCODE, 0, 0); break;
            case 1: SetFocus(button); break;
            case 2: SetFocus(NULL); break;
            case 3: SendMessageA(button, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(4, 4)); break;
            case 4: SendMessageA(button, WM_LBUTTONUP, 0, MAKELPARAM(4, 4)); break;
            case 5: SendMessageA(button, WM_SETFONT, (WPARAM)GetStockObject(SYSTEM_FONT), TRUE); UpdateWindow(button); break;
            }
            recording = 0;
            trace("BUTTON_MESSAGES child=%u visible=%u kind=%u step=%u style=%x result=%x focus=%u capture=%u state=%x events=%s\n",
                  child, visible, kind, step, (unsigned)GetWindowLongA(button, GWL_STYLE) & 15, result, GetFocus() == button, GetCapture() == button,
                  (unsigned)SendMessageA(button, BM_GETSTATE, 0, 0), events);
            fflush(stdout);
        }
        DestroyWindow(button); button = NULL;
    }
    UnhookWindowsHookEx(hhook); DestroyWindow(parent);
}
