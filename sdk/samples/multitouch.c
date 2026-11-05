/* SPDX-License-Identifier: GPL-2.0-or-later
 * python3 neo98.py sdk-build sdk/samples/multitouch.c --gui -o build/MULTITOUCH.EXE */
#include <windows.h>
#include <neo98_touch.h>
static NEO98_TOUCH touch;
static NEO98_TOUCH_EVENT fingers[NEO98_TOUCH_MAX_CONTACTS];
static void contact(void *context, const NEO98_TOUCH_EVENT *event)
{
    unsigned i, slot = NEO98_TOUCH_MAX_CONTACTS;
    (void)context;
    if (event->phase == NEO98_TOUCH_CANCEL_ALL) {
        for (i = 0; i < NEO98_TOUCH_MAX_CONTACTS; ++i) fingers[i].id = 0;
    } else {
        for (i = 0; i < NEO98_TOUCH_MAX_CONTACTS; ++i) {
            if (fingers[i].id == event->id) { slot = i; break; }
            if (!fingers[i].id) slot = i;
        }
        if (slot < NEO98_TOUCH_MAX_CONTACTS) {
            fingers[slot] = *event;
            if (event->phase == NEO98_TOUCH_UP || event->phase == NEO98_TOUCH_CANCEL) fingers[slot].id = 0;
        }
    }
    InvalidateRect(touch.window, 0, TRUE);
}
static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (Neo98TouchHandle(&touch, message, wp, lp, contact, 0)) return 0;
    if (message == WM_PAINT) {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        unsigned i;
        for (i = 0; i < NEO98_TOUCH_MAX_CONTACTS; ++i) if (fingers[i].id) {
            HBRUSH brush = CreateSolidBrush((fingers[i].id * 0x639ad5u) & 0xffffffu);
            HGDIOBJ old = SelectObject(dc, brush);
            Ellipse(dc, fingers[i].x - 18, fingers[i].y - 18, fingers[i].x + 18, fingers[i].y + 18);
            SelectObject(dc, old); DeleteObject(brush);
        }
        EndPaint(window, &paint); return 0;
    }
    // Real mice still work, including the fallback on systems without touch.
    if (message == WM_LBUTTONDOWN || (message == WM_MOUSEMOVE && (wp & 1)) || message == WM_LBUTTONUP) {
        NEO98_TOUCH_EVENT e = {0}; e.id = 0xffffffffu;
        e.x = (short)(lp & 0xffff); e.y = (short)((lp >> 16) & 0xffff);
        e.phase = message == WM_LBUTTONUP ? NEO98_TOUCH_UP : NEO98_TOUCH_DOWN;
        contact(0, &e); return 0;
    }
    if (message == WM_DESTROY) { Neo98TouchClose(&touch); PostQuitMessage(0); return 0; }
    return DefWindowProcA(window, message, wp, lp);
}
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show)
{
    WNDCLASSA klass = {0};
    HWND window;
    MSG message;
    (void)previous; (void)command;
    klass.lpfnWndProc = procedure; klass.hInstance = instance;
    klass.lpszClassName = "NEO98Multitouch"; klass.hbrBackground = (HBRUSH)6;
    if (!RegisterClassA(&klass)) return 1;
    window = CreateWindowExA(0, klass.lpszClassName, "Multitouch", WS_OVERLAPPEDWINDOW,
                             40, 40, 500, 400, 0, 0, instance, 0);
    if (!window || !Neo98TouchOpen(&touch, window, WM_APP + 42)) return 2;
    SetWindowTextA(window, Neo98TouchHasTouchscreen(&touch) ?
                         "Multitouch: touchscreen connected" : "Multitouch: no touchscreen (mouse available)");
    ShowWindow(window, show);
    while (GetMessageA(&message, 0, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageA(&message); }
    return (int)message.wParam;
}
