/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* search and unwind across nested native system callbacks. */
#include <windows.h>
#include <stdio.h>

static int checks, failures, order[32], used, continued;
static void Check(int id, int ok) { ++checks; if (!ok) { ++failures; if (failures < 5) printf("FAIL %d\n", id); } }
static void Mark(int n) { if (used < 32) order[used++] = n; }
static int Filter(int result, EXCEPTION_POINTERS *exception)
{
    /* RaiseException saves a return PC. Keep its call-site flag when the
       dispatcher saves the context for the later unwind or continuation. */
    Check(32, (exception->ContextRecord->ContextFlags & CONTEXT_UNWOUND_TO_CALL) != 0);
    Mark(1);
    return result;
}
static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM w, LPARAM l)
{
    if (message == WM_USER + 1) {
        __try { RaiseException(0xe0001111, 0, 0, NULL); ++continued; }
        __finally { Mark(AbnormalTermination() ? 2 : 8); }
        return 71;
    }
    if (message == WM_USER + 2) {
        __try { SendMessageA(window, WM_USER + 1, 0, 0); Mark(9); }
        __finally { Mark(AbnormalTermination() ? 3 : 9); }
        return 72;
    }
    if (message == WM_USER + 3) {
        __try { SendMessageA(window, WM_USER + 2, 0, 0); }
        __except (Filter(EXCEPTION_EXECUTE_HANDLER, GetExceptionInformation())) { Mark(4); }
        return 73;
    }
    return DefWindowProcA(window, message, w, l);
}
__declspec(noinline) static int Across(HWND window, int resume)
{
    volatile unsigned long long integer = 0x123456789abcdef0ull;
    volatile double real = 3.141592653589793;
    int result = 0;
    __try { result = (int)SendMessageA(window, WM_USER + (resume ? 1 : 2), 0, 0); }
    __except (Filter(resume ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_EXECUTE_HANDLER, GetExceptionInformation())) { result = 93; Mark(4); }
    Check(10, integer == 0x123456789abcdef0ull && real == 3.141592653589793);
    return result;
}
static BOOL CALLBACK Enumerate(HWND window, LPARAM data)
{
    (void)window; (void)data;
    __try { RaiseException(0xe0002222, 0, 0, NULL); }
    __finally { Mark(2); }
    return TRUE;
}
int main(void)
{
    WNDCLASSA c = {0}; c.lpfnWndProc = Procedure;
    c.hInstance = GetModuleHandleA(NULL); c.lpszClassName = "SehCallback";
    RegisterClassA(&c);
    HWND window = CreateWindowExA(0, c.lpszClassName, "", 0, 0, 0, 32, 32, NULL, NULL, c.hInstance, NULL);
    Check(1, window != NULL);
    /* A system procedure is itself a gate: Service returns to Callback
       before the following Step publishes pending == Callback. */
    WNDCLASSA native = c; native.lpfnWndProc = DefWindowProcA; native.lpszClassName = "SehNativeCallback";
    RegisterClassA(&native);
    HWND direct = CreateWindowExA(0, native.lpszClassName, "native", 0, 0, 0, 32, 32, NULL, NULL, native.hInstance, NULL);
    Check(2, direct != NULL);
    Check(3, SendMessageA(direct, WM_GETTEXTLENGTH, 0, 0) == 6);
    DestroyWindow(direct);
    Check(4, UnregisterClassA(native.lpszClassName, native.hInstance));
    for (int i = 0; i < 80; ++i) {
        used = 0;
        Check(20, Across(window, 0) == 93);
        Check(21, used == 4 && order[0] == 1 && order[1] == 2 && order[2] == 3 && order[3] == 4);
        used = 0;
        Check(22, SendMessageA(window, WM_USER + 3, 0, 0) == 73);
        Check(23, used == 4 && order[0] == 1 && order[1] == 2 && order[2] == 3 && order[3] == 4);
        used = 0;
        Check(24, Across(window, 1) == 71);
        Check(25, used == 2 && order[0] == 1 && order[1] == 8 && continued == i + 1);
    }
    used = 0;
    __try { EnumWindows(Enumerate, 0); Mark(9); }
    __except (Filter(EXCEPTION_EXECUTE_HANDLER, GetExceptionInformation())) { Mark(4); }
    Check(30, used == 3 && order[0] == 1 && order[1] == 2 && order[2] == 4);
    DestroyWindow(window);
    Check(31, UnregisterClassA(c.lpszClassName, c.hInstance));
    printf("CALLBACK checks=%d failures=%d\n", checks, failures);
    return failures != 0;
}
