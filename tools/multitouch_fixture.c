/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "../sdk/lib/neo98_touch.h"
static NEO98_TOUCH touch;
static unsigned stage, first, second, mice;
static void check(int ok, unsigned code) { if (!ok) ExitProcess(code); }
static void contact(void *context, const NEO98_TOUCH_EVENT *e)
{
    (void)context;
    check(e->sequence == stage + 1, 90 + stage);
    switch (stage) {
    case 0: check(e->phase == 1 && e->x == 30 && e->y == 40 && e->pressure == 512 && e->flags == 1, 10); first = e->id; break;
    case 1: check(e->phase == 1 && e->id != first && e->x == 60 && e->y == 70 && !e->flags, 11); second = e->id; break;
    case 2: check(e->phase == 2 && e->id == first && e->x == 50 && e->y == 80, 12); break;
    case 3: check(e->phase == 3 && e->id == first, 13); break;
    case 4: check(e->phase == 2 && e->id == second && e->x == 90 && e->y == 110, 14); break;
    case 5: check(e->phase == 3 && e->id == second, 15); break;
    case 6: check(e->phase == 1 && e->id != first && e->id != second, 16); break;
    case 7: check(e->phase == 5 && !e->id, 17); break;
    case 8: check(e->phase == 5 && !e->id, 18); break; // overflow resets the complete unread packet
    case 9: check(e->phase == 1, 19); break;
    case 10: check(!Neo98TouchHasTouchscreen(&touch), 23); /* scripted fingers are not hardware */
             check(e->phase == 3, 20); check(!mice, 21); ExitProcess(0); break;
    default: ExitProcess(22);
    }
    ++stage;
}
static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (Neo98TouchHandle(&touch, message, wp, lp, contact, 0)) return 0;
    if (message >= 0x200 && message <= 0x209) ++mice;
    return DefWindowProcA(window, message, wp, lp);
}
void mainCRTStartup(void)
{
    WNDCLASSA klass = {0};
    HWND window;
    MSG message;
    HINSTANCE instance = GetModuleHandleA(0);
    HMODULE dll;
    typedef BOOL (WINAPI *Caps)(DWORD, void *, DWORD);
    Caps caps;
    NEO98_TOUCH_CAPS answer;
    klass.lpfnWndProc = procedure; klass.hInstance = instance; klass.lpszClassName = "MultitouchFixture";
    check(RegisterClassA(&klass), 1);
    window = CreateWindowExA(0, klass.lpszClassName, "Touch test", WS_POPUP | WS_VISIBLE,
                             40, 40, 320, 240, 0, 0, instance, 0);
    ShowWindow(window, 1);
    check(Neo98TouchOpen(&touch, window, WM_APP + 42) && touch.backend == NEO98_TOUCH_BACKEND_NEO98, 2);
    check(touch.caps.size == 16 && touch.caps.version == 1 && touch.caps.max_contacts == 32, 3);
    check(sizeof(NEO98_TOUCH_EVENT) == 32 && sizeof(NEO98_TOUCH_CAPS) == 16, 4);
    dll = GetModuleHandleA("NEO98TOUCH.DLL"); caps = (Caps)GetProcAddress(dll, "Neo98TouchGetCaps");
    check(!Neo98TouchHasTouchscreen(&touch), 8); // dummy host has no touchscreen
    check(caps && caps(1, &answer, 16) && !(answer.flags & NEO98_TOUCH_CAP_TOUCHSCREEN_PRESENT), 9);
    check(caps && !caps(2, &answer, 16) && !caps(1, &answer, 15), 5);
    check(!touch.subscribe(window, WM_APP + 42, 1), 6);
    check(touch.read(window, &answer, 0) == -1 && touch.read(window, &answer, 65) == -1, 7);
    while (GetMessageA(&message, 0, 0, 0) > 0) DispatchMessageA(&message);
    ExitProcess(80);
}
