/* SPDX-License-Identifier: GPL-2.0-or-later
 * Exercise the native-Windows adapter with supplied pointer records, using
 * the real SDK C code. This is not a Windows 11 hardware test. */
#include <windows.h>
static HMODULE WINAPI fake_load(LPCSTR name);
static void *WINAPI fake_proc(HMODULE module, LPCSTR name);
static int WINAPI fake_metrics(int index);
#define GetSystemMetrics fake_metrics
#define LoadLibraryA fake_load
#define GetProcAddress fake_proc
#include "../sdk/lib/neo98_touch.c"
#undef GetSystemMetrics
#undef LoadLibraryA
#undef GetProcAddress
#ifndef NEO98_WINDOWS_H
/* Independent check against the Windows declarations in mingw-w64. */
#include <stddef.h>
_Static_assert(sizeof(PointerInfo) == sizeof(POINTER_INFO), "POINTER_INFO layout");
_Static_assert(sizeof(PointerTouchInfo) == sizeof(POINTER_TOUCH_INFO), "POINTER_TOUCH_INFO layout");
_Static_assert(offsetof(PointerInfo, performance) == offsetof(POINTER_INFO, PerformanceCount), "performance offset");
_Static_assert(offsetof(PointerTouchInfo, pressure) == offsetof(POINTER_TOUCH_INFO, pressure), "pressure offset");
#endif
static unsigned digitizer;
static int WINAPI fake_metrics(int index) { return index == 94 ? (int)digitizer : 0; }
static PointerInfo pointer;
static unsigned events, phases[12], ids[12], extra;
static BOOL WINAPI info(UINT id, PointerInfo *out) { *out = pointer; return id == pointer.id; }
static BOOL WINAPI touch_info(UINT id, PointerTouchInfo *out)
{
    memset(out, 0, sizeof *out); out->pointer = pointer; out->mask = 4; out->pressure = 333;
    return id == pointer.id;
}
static ULONG_PTR WINAPI message_extra(void) { return extra; }
static BOOL WINAPI logical_point(HWND window, POINT *point)
{
    (void)window; point->x /= 2; point->y /= 2; return TRUE;
}
static int same(const char *a, const char *b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
static HMODULE WINAPI fake_load(LPCSTR name) { (void)name; return 0; }
static void *WINAPI fake_proc(HMODULE module, LPCSTR name)
{
    (void)module;
    if (same(name, "GetPointerInfo")) return (void *)info;
    if (same(name, "GetPointerTouchInfo")) return (void *)touch_info;
    if (same(name, "GetMessageExtraInfo")) return (void *)message_extra;
    if (same(name, "PhysicalToLogicalPointForPerMonitorDPI")) return (void *)logical_point;
    return 0;
}
static void check(int ok, unsigned code) { if (!ok) ExitProcess(code); }
static void contact(void *context, const NEO98_TOUCH_EVENT *e)
{
    (void)context;
    check(events < 12 && e->sequence == events + 1, 10);
    if (e->phase != NEO98_TOUCH_CANCEL_ALL && e->phase != NEO98_TOUCH_CANCEL)
        check(e->x == 30 && e->y == 40 && e->pressure == 333 && e->flags == 1, 11);
    phases[events] = e->phase; ids[events++] = e->id;
}
static void send_pointer(NEO98_TOUCH *s, unsigned message, unsigned id)
{
    pointer.id = id; pointer.type = 2; pointer.flags = 4; pointer.pixel.x = 140; pointer.pixel.y = 160;
    check(Neo98TouchHandle(s, message, id, 0, contact, 0), 12);
}
void mainCRTStartup(void)
{
    WNDCLASSA klass = {0};
    NEO98_TOUCH state;
    HWND window;
    klass.hInstance = GetModuleHandleA(0); klass.lpfnWndProc = DefWindowProcA; klass.lpszClassName = "PointerAdapterFixture";
    check(RegisterClassA(&klass), 1);
    window = CreateWindowExA(0, klass.lpszClassName, "Pointer adapter", WS_POPUP | WS_VISIBLE,
                             40, 40, 320, 240, 0, 0, klass.hInstance, 0);
    check(Neo98TouchOpen(&state, window, WM_APP + 42) && state.backend == NEO98_TOUCH_BACKEND_WINDOWS, 2);
    check(sizeof(PointerInfo) == (sizeof(void *) == 8 ? 96 : 88), 3);
    check(sizeof(PointerTouchInfo) == (sizeof(void *) == 8 ? 144 : 136), 4);
    check(!Neo98TouchHasTouchscreen(&state), 30); // Pointer API alone is not hardware
    digitizer = 0x84; check(!Neo98TouchHasTouchscreen(&state), 31); // ready pen
    digitizer = 1; check(!Neo98TouchHasTouchscreen(&state), 32); // not ready
    digitizer = 0x81; check(Neo98TouchHasTouchscreen(&state), 33); // integrated
    digitizer = 0x82; check(Neo98TouchHasTouchscreen(&state), 34); // external
    digitizer = 0; check(!Neo98TouchHasTouchscreen(&state) &&
        !(state.caps.flags & NEO98_TOUCH_CAP_TOUCHSCREEN_PRESENT), 35); // unplugged
    send_pointer(&state, 0x246, 1); send_pointer(&state, 0x246, 2); send_pointer(&state, 0x247, 1);
    send_pointer(&state, 0x245, 2); send_pointer(&state, 0x247, 2);
    check(events == 5 && phases[0] == 1 && phases[1] == 1 && phases[2] == 3 &&
          phases[3] == 2 && ids[3] == 2 && phases[4] == 3, 5);
    send_pointer(&state, 0x246, 3);
    check(Neo98TouchHandle(&state, 0x24c, 3, 0, contact, 0) && phases[6] == 4, 6);
    send_pointer(&state, 0x246, 4);
    check(!Neo98TouchHandle(&state, WM_KILLFOCUS, 0, 0, contact, 0) && phases[8] == 5, 7);
    send_pointer(&state, 0x245, 4); check(events == 9, 8); // no resurrection after cancel
    extra = 0xff515780u;
    check(Neo98TouchHandle(&state, WM_LBUTTONDOWN, 0, 0, contact, 0), 9);
    extra = 0;
    check(!Neo98TouchHandle(&state, WM_LBUTTONDOWN, 0, 0, contact, 0), 20);
    Neo98TouchClose(&state); check(!Neo98TouchHasTouchscreen(&state), 36); ExitProcess(0);
}
