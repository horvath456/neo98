/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U VirtualAlloc(void *, U, U, U);
API U VirtualFree(void *, U, U);
API U VirtualProtect(void *, U, U, U *);
API U VirtualQuery(const void *, void *, U);
API void SetLastError(U);
API U GetLastError(void);
API void ExitProcess(U);
API char *GetCommandLineA(void);
API U lstrcpyA(char *, const char *);
typedef struct
{
    U base, allocation, allocation_protection, size, state, protection, type;
} Info;
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
void run_virtual_test(void)
{
    SetLastError(98);
    U base =
        VirtualAlloc(0, 0x10000000, 0x2000, 4); /* 256 MiB reserved; only four pages will be committed. */
    check(base && !(base & 65535) && GetLastError() == 98, 3000);
    Info i;
    check(VirtualQuery((void *)(base + 12), &i, sizeof i) == sizeof i && i.base == base &&
              i.allocation == base && i.size == 0x10000000 && i.state == 0x2000 && !i.protection,
          3001);
    check(VirtualAlloc((void *)(base + 4095), 2, 0x1000, 4) == base, 3002);
    check(VirtualQuery((void *)base, &i, sizeof i) && i.size == 8192 && i.state == 0x1000 &&
              i.protection == 4,
          3003);
    volatile unsigned char *p = (void *)base;
    for (U n = 0; n < 8192; ++n)
        check(!p[n], 3004);
    p[0] = 98;
    p[4096] = 97;
    check(VirtualAlloc((void *)base, 8192, 0x1000, 2) == base && p[0] == 98 && p[4096] == 97, 3005);
    check(VirtualQuery((void *)base, &i, sizeof i) && i.protection == 4,
          3006); /* Recommit preserves live pages. */
    U old = 0;
    check(VirtualProtect((void *)(base + 4096), 4096, 2, &old) == 4 && old == 4, 3007);
    check(VirtualQuery((void *)base, &i, sizeof i) && i.size == 4096 && i.protection == 4, 3008);
    check(VirtualQuery((void *)(base + 4096), &i, sizeof i) && i.protection == 2, 3009);
    check(p[4096] == 97 && VirtualProtect((void *)(base + 4096), 4096, 4, 0) == 2, 3010);
    old = 98;
    check(!VirtualProtect((void *)(base + 4096), 8192, 2, &old) && old == 98 && GetLastError() == 87, 3011);
    check(VirtualFree((void *)(base + 4096), 8192, 0x4000), 3012);
    check(VirtualQuery((void *)(base + 4096), &i, sizeof i) && i.state == 0x2000 &&
              i.size == 0x10000000 - 4096,
          3013);
    check(VirtualAlloc((void *)(base + 4096), 4096, 0x1000, 4) == base + 4096 && p[4096] == 0, 3014);
    check(!VirtualFree((void *)(base + 4096), 0, 0x8000) && GetLastError() == 487, 3015);
    check(!VirtualFree((void *)base, 0, 0x4000) && GetLastError() == 87, 3016);
    check(VirtualFree((void *)base, 0, 0x8000), 3017);
    check(VirtualQuery((void *)base, &i, sizeof i) && i.state == 0x10000 && !i.allocation, 3018);
    U code = VirtualAlloc((void *)base, 4096, 0x3000, 4);
    check(code == base, 3019);
    unsigned char *program = (void *)code;
    program[0] = 0xb8;
    program[1] = 98;
    program[2] = program[3] = program[4] = 0;
    program[5] = 0xc3;
    /* Win98 VMM pages have no NX bit: generated x86 also executes on PAGE_READWRITE. */
    check(((U(__cdecl *)(void))code)() == 98, 3020);
    check(VirtualProtect((void *)code, 4096, 0x20, &old) == 4 && old == 4, 3021);
    check(((U(__cdecl *)(void))code)() == 98, 3022);
    check(VirtualQuery((void *)code, &i, sizeof i) && i.protection == 2, 3023);
    check(VirtualFree((void *)code, 0, 0x8000), 3024);
    check(!VirtualAlloc((void *)0xc0000000, 4096, 0x3000, 4) && GetLastError() == 487, 3025);
    check(!VirtualAlloc(0, 4096, 0x3000, 0x104) && GetLastError() == 87, 3026);
    check(VirtualQuery((void *)0x20000000, &i, sizeof i) && i.state == 0x2000, 3027);
    check(!VirtualAlloc((void *)0x20000000, 4096, 0x2000, 4), 3028);
    for (U n = 0; n < 270; ++n)
    {
        U a = VirtualAlloc(0, 4096, 0x3000, 4);
        check(a && !*(U *)a && VirtualFree((void *)a, 0, 0x8000), 3029);
    }
}
#ifdef VIRTUAL_STANDALONE
void mainCRTStartup(void)
{
    const char mode = *GetCommandLineA();
    if (mode == '0')
    {
        run_virtual_test();
        ExitProcess(0);
    }
    U a = VirtualAlloc(0, 4096, 0x3000, 4), old;
    check(a, 3090);
    if (mode == '1')
    {
        check(VirtualProtect((void *)a, 4096, 2, &old), 3091);
        *(volatile U *)a = 98; /* CPU write into read-only memory must fail. */
    }
    if (mode == '2')
    {
        check(VirtualProtect((void *)a, 4096, 2, &old), 3092);
        lstrcpyA((char *)a, "protected"); /* Native API writes must obey the same protection. */
    }
    if (mode == '3')
    {
        check(VirtualFree((void *)a, 4096, 0x4000), 3093);
        old = *(volatile U *)a; /* Decommit removes backing, rather than merely changing metadata. */
    }
    if (mode == '4')
    {
        check(VirtualProtect((void *)a, 4096, 1, &old), 3094);
        ((U(__cdecl *)(void))a)();
    }
    ExitProcess(3099);
}
#endif
