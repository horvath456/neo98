/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
// imported libc++abi/libunwind across native callback frames.
#include <windows.h>
#include <stdio.h>

static int checks, failures, order[16], used;
static void Check(int id, bool ok) { ++checks; if (!ok) { ++failures; printf("FAIL %d\n", id); } }
struct Guard {
    int id;
    ~Guard() { if (used < 16) order[used++] = id; }
};
static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM, LPARAM)
{
    if (message == WM_USER + 1) { Guard g{1}; throw 42; }
    if (message == WM_USER + 2) { Guard g{2}; return SendMessageA(window, WM_USER + 1, 0, 0); }
    if (message == WM_USER + 3) {
        Guard g{3};
        try { SendMessageA(window, WM_USER + 2, 0, 0); }
        catch (int value) { Check(10, value == 42); return 73; }
    }
    return DefWindowProcA(window, message, 0, 0);
}
__declspec(noinline) static void Across(HWND window, unsigned long long seed, double number)
{
    register unsigned long long integer __asm__("x19") = seed;
    register double real __asm__("d8") = number;
    __asm__ volatile("" : "+r"(integer), "+w"(real));
    try { Guard g{4}; SendMessageA(window, WM_USER + 2, 0, 0); Check(11, false); }
    catch (int value) { Check(12, value == 42); }
    __asm__ volatile("" : "+r"(integer), "+w"(real));
    Check(13, integer == seed && real == number);
}
int main()
{
    WNDCLASSA c = {}; c.lpfnWndProc = Procedure; c.hInstance = GetModuleHandleA(nullptr); c.lpszClassName = "CxxCallback";
    RegisterClassA(&c);
    HWND window = CreateWindowExA(0, c.lpszClassName, "", 0, 0, 0, 32, 32, nullptr, nullptr, c.hInstance, nullptr);
    Check(1, window != nullptr);
    for (int i = 0; i < 80; ++i) {
        used = 0;
        Across(window, 0x123456789abcdef0ull + i, 3.25 + i);
        Check(20, used == 3 && order[0] == 1 && order[1] == 2 && order[2] == 4);
        used = 0;
        Check(21, SendMessageA(window, WM_USER + 3, 0, 0) == 73);
        Check(22, used == 3 && order[0] == 1 && order[1] == 2 && order[2] == 3);
    }
    DestroyWindow(window);
    Check(2, UnregisterClassA(c.lpszClassName, c.hInstance));
    printf("CXXCALLBACK checks=%d failures=%d\n", checks, failures);
    return failures != 0;
}
