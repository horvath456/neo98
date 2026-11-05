/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NEO98_TOUCH_H
#define NEO98_TOUCH_H
#include <windows.h>
#include "neo98_touch_abi.h"
#ifdef __cplusplus
extern "C" {
#endif
#define NEO98_TOUCH_BACKEND_MOUSE 0u
#define NEO98_TOUCH_BACKEND_NEO98 1u
#define NEO98_TOUCH_BACKEND_WINDOWS 2u
typedef void (*NEO98_TOUCH_CALLBACK)(void *, const NEO98_TOUCH_EVENT *);
typedef BOOL (WINAPI *NEO98_TOUCH_REGISTER_FN)(HWND, UINT, DWORD);
typedef BOOL (WINAPI *NEO98_TOUCH_UNREGISTER_FN)(HWND);
typedef int (WINAPI *NEO98_TOUCH_READ_FN)(HWND, void *, UINT);
/* Fields are SDK internals. Allocate one context per window on its UI thread. */
typedef struct NEO98_TOUCH {
    HWND window;
    HMODULE module;
    UINT message, backend;
    NEO98_TOUCH_CAPS caps;
    NEO98_TOUCH_REGISTER_FN subscribe;
    NEO98_TOUCH_UNREGISTER_FN unsubscribe;
    NEO98_TOUCH_READ_FN read;
    void *pointer_info, *pointer_touch_info, *extra_info, *physical_to_logical;
    unsigned active[NEO98_TOUCH_MAX_CONTACTS], sequence;
} NEO98_TOUCH;
/* Open succeeds even without hardware. backend describes API support only. */
BOOL Neo98TouchOpen(NEO98_TOUCH *, HWND, UINT notification_message);
void Neo98TouchClose(NEO98_TOUCH *);
/* Fresh hardware query on the window UI thread; trackpads/pens do not count.
 * Also refreshes caps.flags. FALSE includes unavailable/closed contexts. */
BOOL Neo98TouchHasTouchscreen(NEO98_TOUCH *);
/* Call before ordinary mouse handling / DefWindowProc. TRUE means consumed.
 * Calls the supplied callback for each event. It may not destroy the context
 * or recursively process messages; queue UI changes until after this returns. */
BOOL Neo98TouchHandle(NEO98_TOUCH *, UINT, WPARAM, LPARAM, NEO98_TOUCH_CALLBACK, void *);
#ifdef __cplusplus
}
#endif
#endif
