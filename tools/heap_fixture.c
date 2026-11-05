/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U HeapCreate(U, U, U);
API U HeapDestroy(U);
API U GetProcessHeap(void);
API void *HeapAlloc(U, U, U);
API void *HeapReAlloc(U, U, void *, U);
API U HeapFree(U, U, void *);
API U HeapSize(U, U, void *);
API U HeapValidate(U, U, void *);
API U GetLastError(void);
API void SetLastError(U);
API U GetVersion(void);
API U GetProcessVersion(U);
API U GetModuleHandleA(const char *);
API U SetHandleCount(U);
API U GetStdHandle(U);
API U GetFileType(U);
API U GetCurrentProcessId(void);
API U VirtualQuery(const void *, void *, U);
API U VirtualFree(void *, U, U);
API U VirtualProtect(void *, U, U, U *);
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static void original_shared_heap(void)
{
    U h = HeapCreate(0x04000000, 0, 0), q[7];
    check(h >= 0x80000000 && h < 0xc0000000, 4100);
    check(*(U *)h == 0x100000 && *(unsigned short *)(h + 0x72) == 0x4948, 4101);
    check(VirtualQuery((void *)h, q, sizeof q) == sizeof q && q[4] == 0x1000 && q[6] == 0x20000, 4102);
    check(!HeapFree(h, 0, (void *)(h + 0x2000)) && GetLastError() == 87, 4117);
    check(!HeapSize(h, 0, (void *)(h + 0x2000)) && GetLastError() == 87, 4118);
    check(!HeapReAlloc(h, 0, (void *)(h + 0x2000), 16) && GetLastError() == 87, 4119);
    check(!VirtualFree((void *)h, 0, 0x8000) && !VirtualFree((void *)(h + 0x2000), 4096, 0x4000), 4103);
    unsigned char *p = HeapAlloc(h, 8, 0x180000); // Requires the original 4-MiB extension.
    check(p && *(U *)(h + 4) && *(U *)(*(U *)(h + 4)) == 0x400000, 4104);
    check(HeapSize(h, 0, p) == 0x180000, 4105);
    for (U i = 0; i < 0x180000; i += 4093)
        check(!p[i], 4106);
    p[0] = 0x98;
    p[0x17ffff] = 0xa4;
    U extension = *(U *)(h + 4);
    check(HeapFree(h, 0, p), 4107);
    check(VirtualQuery((void *)(extension + 0x2000), q, sizeof q) == sizeof q && q[4] == 0x2000, 4108);
    p = HeapAlloc(h, 8, 0x180000);
    check(p && !p[0] && !p[0x17ffff], 4109);
    check(HeapDestroy(h), 4110); // Frees live allocations and every extension.
    check(VirtualQuery((void *)h, q, sizeof q) == sizeof q && q[4] == 0x10000, 4111);
    check(VirtualQuery((void *)extension, q, sizeof q) == sizeof q && q[4] == 0x10000, 4112);
    // Fixed heaps reuse/coalesce freed neighbors and expose actual payload capacity.
    h = HeapCreate(1, 0, 4096);
    void *blocks[24];
    for (U i = 0; i < 24; ++i)
    {
        blocks[i] = HeapAlloc(h, 0, i + 1);
        check(blocks[i] && HeapSize(h, 0, blocks[i]) == (i < 12 ? 12u : (i + 4) & ~3u), 4113);
    }
    U old;
    check(VirtualProtect((void *)h, 4096, 2, &old) && old == 4, 4120);
    check(HeapSize(h, 0, blocks[0]) == 12, 4121);
    check(VirtualProtect((void *)h, 4096, 4, &old) && old == 2, 4122);
    for (U i = 0; i < 24; i += 2)
        check(HeapFree(h, 0, blocks[i]), 4114);
    for (U i = 1; i < 24; i += 2)
        check(HeapFree(h, 0, blocks[i]), 4115);
    check(HeapAlloc(h, 0, 3968) != 0 && !HeapAlloc(h, 0, 1) && HeapDestroy(h), 4116);
}
void run_heap_test(void)
{
    check(GetVersion() == 0xc0000a04 && SetHandleCount(1024) == 256 && !SetHandleCount(0), 601);
    unsigned char *exe = (unsigned char *)GetModuleHandleA(0);
    U pe = *(U *)(exe + 60);
    unsigned short *version = (unsigned short *)(exe + pe + 24 + 48);
    U expected = ((version[0] & 255) << 16) | (version[1] & 255);
    SetLastError(98);
    /* Zero means the calling process. Process identities are handed out by the
       runtime and do not restart at one for every program, so ask for the
       current one instead of assuming it. */
    check(GetProcessVersion(0) == expected && GetProcessVersion(GetCurrentProcessId()) == expected &&
              GetLastError() == 98,
          621);
    check(!GetProcessVersion(~0u) && GetLastError() == 87, 622);
    // COMMON.EXE has the console subsystem and receives real console handles.
    for (U standard = 10; standard <= 12; ++standard)
    {
        U handle = GetStdHandle(0u - standard);
        check(handle && handle != ~0u && GetFileType(handle) == 2, 620);
    }
    U h = HeapCreate(0, 0, 4096), other = HeapCreate(0, 0, 0);
    check(h && other && h != other && GetProcessHeap() != h, 602);
    unsigned char *p = HeapAlloc(h, 8, 17);
    check(p && HeapSize(h, 0, p) == 20 && HeapValidate(h, 0, p) == ~0u && GetLastError() == 120, 603);
    for (U i = 0; i < 20; ++i)
        check(!p[i], 604);
    for (U i = 0; i < 17; ++i)
        p[i] = i + 1;
    check(!HeapFree(other, 0, p) && !HeapSize(other, 0, p) && GetLastError() == 87, 605);
    p = HeapReAlloc(h, 8, p, 24);
    check(p != 0, 606);
    for (U i = 0; i < 24; ++i)
        check(p[i] == (i < 17 ? i + 1 : 0), 607);
    void *blocker = HeapAlloc(h, 0, 64); // Same heap: prevents growth into the following free block.
    check(blocker != 0, 608);
    SetLastError(0x9898);
    check(!HeapReAlloc(h, 0x10, p, 512) && GetLastError() == 212, 609);
    for (U i = 0; i < 17; ++i)
        check(p[i] == i + 1, 610);
    unsigned char *moved = HeapReAlloc(h, 8, p, 512);
    check(moved && moved != p && HeapSize(h, 0, moved) == 512, 611);
    for (U i = 0; i < 512; ++i)
        check(moved[i] == (i < 17 ? i + 1 : 0), 612);
    SetLastError(0x9876);
    check(!HeapAlloc(h, 0, 4096) && GetLastError() == 8, 613);
    check(!HeapFree(h, 0, 0) && GetLastError() == 87 && HeapFree(h, 0, moved) && !HeapFree(h, 0, moved), 614);
    check(HeapFree(h, 0, blocker) && HeapAlloc(h, 0, 3000) != 0 && HeapDestroy(h), 615);
    check(!HeapDestroy(h) && !HeapAlloc(h, 0, 16), 616);
    check(HeapDestroy(other), 617);
    U fresh = HeapCreate(0, 0, 0); // Original pointer handles can reuse a released address.
    check(fresh && HeapDestroy(fresh), 618);
    p = HeapAlloc(GetProcessHeap(), 8, 0);
    check(p && HeapSize(GetProcessHeap(), 0, p) == 12 && HeapFree(GetProcessHeap(), 0, p), 619);
    original_shared_heap();
}
