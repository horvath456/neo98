/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Small x86 import fixture. The runner executes these calls through the
   USER32 import gate, including WindowFromDC itself. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall

API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API U RegisterClassA(void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int,
                      U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U DestroyWindow(U);
API U GetDC(U);
API U WindowFromDC(U);
API U ReleaseDC(U, U);

static void check(U value, U code)
{
    if (!value) ExitProcess(code);
}

static U __stdcall procedure(U hwnd, U message, U wparam, U lparam)
{
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

void mainCRTStartup(void)
{
    U instance = GetModuleHandleA(0);
    U fields[10] = {0, (U)procedure, 0, 0, instance, 0, 0, 0, 0,
                    (U)"WindowFromDCGuest"};
    check(RegisterClassA(fields), 18300);

    /* GetDC(NULL) uses the shared screen DC, but the original USER lookup
       has no window record for this direct lease. */
    U screen = GetDC(0);
    check(screen && WindowFromDC(screen) == 0, 18301);
    check(ReleaseDC(0, screen) && WindowFromDC(screen) == 0, 18302);

    U window = CreateWindowExA(0, "WindowFromDCGuest", "guest", 0x90000000,
                               20, 20, 120, 80, 0, 0, instance, 0);
    check(window, 18303);
    U dc = GetDC(window);
    check(dc && WindowFromDC(dc) == window, 18304);
    check(ReleaseDC(window, dc) && WindowFromDC(dc) == 0, 18305);

    check(DestroyWindow(window), 18306);
    ExitProcess(0);
}
