/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Many small allocations, fragmentation, and reuse across a saved state. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U GlobalAlloc(U, U);
API U GlobalLock(U);
API U GlobalSize(U);
API U GlobalFree(U);
API U GlobalReAlloc(U, U, U);
API U MessageBoxA(U, const char *, const char *, U);
API void ExitProcess(U);
static U handles[17000];
static void check(int yes, U code) { if (!yes) ExitProcess(code); }
void mainCRTStartup(void)
{
    for (U i = 0; i < 17000; ++i) {
        U h = handles[i] = GlobalAlloc(0x2042, 132);
        check(h != 0, 56131);
        U *p = (U *)GlobalLock(h);
        check(p && !p[0] && !p[32], 56132);
        p[0] = i; p[32] = ~i;
    }
    for (U i = 1; i < 17000; i += 2) {
        check(GlobalFree(handles[i]) == 0, 56133);
        handles[i] = 0;
    }
    MessageBoxA(0, "Continue to check and free the surviving blocks.", "Heap metadata ready", 0);
    for (U i = 0; i < 17000; i += 2) {
        U *p = (U *)GlobalLock(handles[i]);
        check(p && p[0] == i && p[32] == ~i && GlobalSize(handles[i]) >= 132, 56134);
        handles[i] = GlobalReAlloc(handles[i], 260, 0x42);
        p = (U *)GlobalLock(handles[i]);
        check(p && p[0] == i && p[32] == ~i && p[64] == 0, 56135);
    }
    for (U i = 0; i < 17000; i += 2) check(GlobalFree(handles[i]) == 0, 56136);
    /* Coalescing must make the freed storage usable for a larger block. */
    U h = GlobalAlloc(0, 17000 * 144);
    check(h && GlobalSize(h) >= 17000 * 144 && !GlobalFree(h), 56137);
    ExitProcess(0);
}
