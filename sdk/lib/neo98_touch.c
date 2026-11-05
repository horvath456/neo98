/* SPDX-License-Identifier: GPL-2.0-or-later
 * Native Pointer layouts follow winuser.h. Modern functions are optional
 * dynamic imports, so this object also loads on NEO98 and old Windows. */
#include "neo98_touch.h"
#include <string.h>
typedef BOOL (WINAPI *CapsFn)(DWORD, void *, DWORD);
typedef struct PointerInfo {
    unsigned type, id, frame, flags;
    HANDLE source;
    HWND target;
    POINT pixel, himetric, raw_pixel, raw_himetric;
    DWORD time, history;
    int input;
    DWORD keys;
    unsigned long long performance;
    int button;
} PointerInfo;
typedef struct PointerTouchInfo {
    PointerInfo pointer;
    unsigned flags, mask;
    RECT contact, raw_contact;
    unsigned orientation, pressure;
} PointerTouchInfo;
typedef BOOL (WINAPI *PointerFn)(UINT, PointerInfo *);
typedef BOOL (WINAPI *PointerTouchFn)(UINT, PointerTouchInfo *);
typedef ULONG_PTR (WINAPI *ExtraFn)(void);
typedef BOOL (WINAPI *PointFn)(HWND, POINT *);
static void emit(NEO98_TOUCH *s, NEO98_TOUCH_EVENT *e, NEO98_TOUCH_CALLBACK callback, void *context)
{
    unsigned i, free_slot = NEO98_TOUCH_MAX_CONTACTS;
    if (e->phase == NEO98_TOUCH_CANCEL_ALL) {
        memset(s->active, 0, sizeof s->active);
    } else {
        for (i = 0; i < NEO98_TOUCH_MAX_CONTACTS; ++i) {
            if (s->active[i] == e->id) break;
            if (!s->active[i]) free_slot = i;
        }
        if (e->phase == NEO98_TOUCH_DOWN) {
            if (i < NEO98_TOUCH_MAX_CONTACTS) return;
            if (free_slot == NEO98_TOUCH_MAX_CONTACTS) {
                NEO98_TOUCH_EVENT cancel = {0};
                cancel.phase = NEO98_TOUCH_CANCEL_ALL;
                emit(s, &cancel, callback, context);
                return;
            }
            s->active[free_slot] = e->id;
        } else {
            if (i == NEO98_TOUCH_MAX_CONTACTS) return;
            if (e->phase == NEO98_TOUCH_UP || e->phase == NEO98_TOUCH_CANCEL) s->active[i] = 0;
        }
    }
    e->sequence = ++s->sequence;
    callback(context, e);
}
BOOL Neo98TouchOpen(NEO98_TOUCH *s, HWND window, UINT message)
{
    HMODULE user;
    CapsFn caps;
    if (!s || !IsWindow(window) || message < 0x8000 || message > 0xffff) return FALSE;
    memset(s, 0, sizeof *s);
    s->window = window; s->message = message;
    s->module = LoadLibraryA("NEO98TOUCH.DLL");
    if (s->module) {
        caps = (CapsFn)GetProcAddress(s->module, "Neo98TouchGetCaps");
        s->subscribe = (NEO98_TOUCH_REGISTER_FN)GetProcAddress(s->module, "Neo98TouchRegister");
        s->unsubscribe = (NEO98_TOUCH_UNREGISTER_FN)GetProcAddress(s->module, "Neo98TouchUnregister");
        s->read = (NEO98_TOUCH_READ_FN)GetProcAddress(s->module, "Neo98TouchRead");
        if (caps && s->subscribe && s->unsubscribe && s->read &&
            caps(NEO98_TOUCH_VERSION, &s->caps, sizeof s->caps) &&
            s->caps.version == NEO98_TOUCH_VERSION && s->subscribe(window, message, 0)) {
            s->backend = NEO98_TOUCH_BACKEND_NEO98;
            return TRUE;
        }
        FreeLibrary(s->module); s->module = 0;
    }
    user = GetModuleHandleA("USER32.DLL");
    s->pointer_info = GetProcAddress(user, "GetPointerInfo");
    s->pointer_touch_info = GetProcAddress(user, "GetPointerTouchInfo");
    s->extra_info = GetProcAddress(user, "GetMessageExtraInfo");
    s->physical_to_logical = GetProcAddress(user, "PhysicalToLogicalPointForPerMonitorDPI");
    if (s->pointer_info) {
        s->backend = NEO98_TOUCH_BACKEND_WINDOWS;
        s->caps.size = sizeof s->caps; s->caps.version = NEO98_TOUCH_VERSION;
        s->caps.max_contacts = NEO98_TOUCH_MAX_CONTACTS; s->caps.flags = NEO98_TOUCH_CAP_MULTITOUCH;
    }
    Neo98TouchHasTouchscreen(s);
    return TRUE;
}
BOOL Neo98TouchHasTouchscreen(NEO98_TOUCH *s)
{
    if (!s || !s->window) return FALSE;
    s->caps.flags &= ~NEO98_TOUCH_CAP_TOUCHSCREEN_PRESENT;
    if (s->backend == NEO98_TOUCH_BACKEND_NEO98 && s->module) {
        CapsFn caps = (CapsFn)GetProcAddress(s->module, "Neo98TouchGetCaps");
        NEO98_TOUCH_CAPS current;
        if (caps && caps(NEO98_TOUCH_VERSION, &current, sizeof current)) s->caps = current;
    } else if (s->backend == NEO98_TOUCH_BACKEND_WINDOWS) {
        unsigned digitizer = (unsigned)GetSystemMetrics(94); /* SM_DIGITIZER */
        /* NID_READY and integrated/external touch; pen-only is insufficient. */
        if ((digitizer & 0x80u) && (digitizer & 3u))
            s->caps.flags |= NEO98_TOUCH_CAP_TOUCHSCREEN_PRESENT;
    }
    return (s->caps.flags & NEO98_TOUCH_CAP_TOUCHSCREEN_PRESENT) != 0;
}
void Neo98TouchClose(NEO98_TOUCH *s)
{
    if (!s) return;
    if (s->backend == NEO98_TOUCH_BACKEND_NEO98) s->unsubscribe(s->window);
    if (s->module) FreeLibrary(s->module);
    memset(s, 0, sizeof *s);
}
BOOL Neo98TouchHandle(NEO98_TOUCH *s, UINT message, WPARAM wp, LPARAM lp,
                     NEO98_TOUCH_CALLBACK callback, void *context)
{
    unsigned i;
    (void)lp;
    if (!s || !s->window || !callback) return FALSE;
    if (message == 0x1f || message == 8 || (message == 6 && !(wp & 0xffff)) || message == 2) {
        for (i = 0; i < NEO98_TOUCH_MAX_CONTACTS && !s->active[i]; ++i) {}
        if (i < NEO98_TOUCH_MAX_CONTACTS) {
            NEO98_TOUCH_EVENT e = {0}; e.phase = NEO98_TOUCH_CANCEL_ALL;
            emit(s, &e, callback, context);
        }
        return FALSE;
    }
    if (s->backend == NEO98_TOUCH_BACKEND_NEO98 && message == s->message) {
        NEO98_TOUCH_EVENT events[16];
        int count;
        while ((count = s->read(s->window, events, 16)) > 0)
            for (i = 0; i < (unsigned)count; ++i) emit(s, &events[i], callback, context);
        if (count < 0) {
            NEO98_TOUCH_EVENT e = {0}; e.phase = NEO98_TOUCH_CANCEL_ALL;
            emit(s, &e, callback, context);
        }
        return TRUE;
    }
    if (s->backend != NEO98_TOUCH_BACKEND_WINDOWS) return FALSE;
    if (message >= 0x200 && message <= 0x20e && s->extra_info) {
        /* MI_WP_SIGNATURE with its touch bit: leave real mice and pens alone. */
        ULONG_PTR extra = ((ExtraFn)s->extra_info)();
        if ((extra & 0xffffff80u) == 0xff515780u) return TRUE;
    }
    if (message == 0x24c) { // WM_POINTERCAPTURECHANGED
        NEO98_TOUCH_EVENT e = {0}; e.id = (unsigned)wp & 0xffff; e.phase = NEO98_TOUCH_CANCEL;
        emit(s, &e, callback, context); return TRUE;
    }
    if (message == 0x246 || message == 0x245 || message == 0x247) {
        PointerInfo p;
        NEO98_TOUCH_EVENT e = {0};
        if (!((PointerFn)s->pointer_info)((UINT)wp & 0xffff, &p)) {
            e.phase = NEO98_TOUCH_CANCEL_ALL; emit(s, &e, callback, context); return TRUE;
        }
        if (p.type != 2) return FALSE; // PT_TOUCH
        if (message == 0x245 && !(p.flags & 4) && !(p.flags & 0x8000)) return TRUE;
        // Pointer locations are physical screen pixels. Match the window's
        // logical coordinate space before ScreenToClient on scaled Windows.
        if (s->physical_to_logical) ((PointFn)s->physical_to_logical)(s->window, &p.pixel);
        ScreenToClient(s->window, &p.pixel);
        e.id = p.id; e.x = p.pixel.x; e.y = p.pixel.y; e.time = p.time;
        e.phase = p.flags & 0x8000 ? NEO98_TOUCH_CANCEL : message == 0x246 ? NEO98_TOUCH_DOWN :
                  message == 0x247 ? NEO98_TOUCH_UP : NEO98_TOUCH_MOVE;
        if (s->pointer_touch_info) {
            PointerTouchInfo touch;
            if (((PointerTouchFn)s->pointer_touch_info)(p.id, &touch) && (touch.mask & 4)) {
                e.flags = NEO98_TOUCH_PRESSURE_VALID;
                e.pressure = touch.pressure > 1024 ? 1024 : touch.pressure;
            }
        }
        emit(s, &e, callback, context);
        return TRUE;
    }
    return FALSE;
}
