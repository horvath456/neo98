/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
typedef unsigned short W;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U lstrlenW(const W *);
API W *lstrcpyW(W *, const W *);
API W *lstrcatW(W *, const W *);
API W *lstrcpynW(W *, const W *, int);
API void SetLastError(U);
API U GetLastError(void);
API void *VirtualAlloc(void *, U, U, U);
API U VirtualFree(void *, U, U);
API U VirtualProtect(void *, U, U, U *);
static void check(int condition, U code)
{
    if (!condition)
        ExitProcess(code);
}
void run_wstrings_test(void)
{
    const W source[] = {0x98, 0x20ac, 0xd83d, 0xde00, 0};
    W destination[16] = {0x98, 0};
    SetLastError(98);
    check(lstrlenW(source) == 4 && GetLastError() == 98, 4200);
    check(lstrcpyW(destination, source) == destination && lstrlenW(destination) == 4, 4201);
    check(lstrcatW(destination, source) == destination && lstrlenW(destination) == 8, 4202);
    check(destination[5] == 0x20ac && destination[6] == 0xd83d && destination[7] == 0xde00, 4203);
    check(!lstrlenW(0) && GetLastError() == 87, 4204);
    check(!lstrcpyW(0, source) && GetLastError() == 87, 4205);
    check(!lstrcpynW((W *)~0u, (const W *)~0u, 16) && GetLastError() == 120, 4206);
    W *pages = VirtualAlloc(0, 8192, 0x3000, 4);
    check(pages != 0, 4207);
    pages[2047] = 0x20ac;
    U old;
    check(VirtualProtect(pages + 2048, 4096, 1, &old), 4208);
    destination[0] = destination[1] = 0x5555;
    check(!lstrcpyW(destination, pages + 2047) && GetLastError() == 87, 4209);
    check(destination[0] == 0x20ac && destination[1] == 0x5555, 4210); // Keep completed stores on fault.
    check(!lstrcpyW(pages + 2047, source) && GetLastError() == 87 && pages[2047] == source[0], 4211);
    check(!lstrlenW(pages + 2047) && GetLastError() == 87, 4212);
    check(VirtualFree(pages, 0, 0x8000), 4213);
}
