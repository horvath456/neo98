/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The i386 of the Win9x era has no execute-disable bit, so a page a process
   can read it can also execute. This fixture puts x86 code into a LocalAlloc
   block, a HeapAlloc block, a VirtualAlloc region with PAGE_READWRITE and a
   local array on the stack, runs it, overwrites it and runs it again, so the
   second run has to see the new code. It also shows that read-only stays
   readable and executable while writing to it is still refused.
    */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U LocalAlloc(U, U);
API U LocalFree(U);
API U GetProcessHeap(void);
API U HeapAlloc(U, U, U);
API U HeapFree(U, U, U);
API U VirtualAlloc(void *, U, U, U);
API U VirtualFree(void *, U, U);
API U VirtualProtect(void *, U, U, U *);
API U VirtualQuery(const void *, void *, U);
API U IsBadReadPtr(const void *, U);
API U IsBadWritePtr(void *, U);
API U IsBadCodePtr(U);
API U GetLastError(void);
API void SetLastError(U);
API void ExitProcess(U);
API char *GetCommandLineA(void);

typedef struct
{
    U base, allocation, allocation_protection, size, state, protection, type;
} Info;

typedef U(__cdecl *Routine)(void);

static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}

/* mov eax, value; ret */
static void emit(unsigned char *p, U value)
{
    p[0] = 0xb8;
    p[1] = (unsigned char)value;
    p[2] = (unsigned char)(value >> 8);
    p[3] = (unsigned char)(value >> 16);
    p[4] = (unsigned char)(value >> 24);
    p[5] = 0xc3;
}

/* Write code, run it, overwrite it through the CPU and run it again. The
   second call must see the second value, so the translation of the first
   version cannot survive the write. */
static void rewrite_and_run(unsigned char *p, U first, U second, U code)
{
    emit(p, first);
    check(((Routine)p)() == first, code);
    emit(p, second);
    check(((Routine)p)() == second, code + 1);
    emit(p, first + second);
    check(((Routine)p)() == first + second, code + 2);
}

static void run_executable_pages_test(void)
{
    U block = LocalAlloc(0, 64); /* LMEM_FIXED, process heap */
    check(block != 0, 4400);
    rewrite_and_run((unsigned char *)block, 0x7001, 0x7002, 4401);
    check(LocalFree(block) == 0, 4404);

    U heap = GetProcessHeap();
    check(heap != 0, 4405);
    U hblock = HeapAlloc(heap, 0, 64);
    check(hblock != 0, 4406);
    rewrite_and_run((unsigned char *)hblock, 0x7101, 0x7102, 4407);
    check(HeapFree(heap, 0, hblock) != 0, 4410);

    U region = VirtualAlloc(0, 4096, 0x3000, 4); /* MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE */
    check(region != 0, 4411);
    rewrite_and_run((unsigned char *)region, 0x7201, 0x7202, 4412);

    /* Read-only keeps its read access, so it keeps its execute access. */
    U old = 0;
    check(VirtualProtect((void *)region, 4096, 2, &old) && old == 4, 4415);
    check(((Routine)region)() == 0x7201 + 0x7202, 4416);
    Info i;
    check(VirtualQuery((void *)region, &i, sizeof i) == sizeof i && i.protection == 2, 4417);
    SetLastError(98);
    check(IsBadReadPtr((void *)region, 6) == 0, 4418);
    check(IsBadCodePtr(region) == 0, 4419);
    check(IsBadWritePtr((void *)region, 6) != 0, 4420); /* Write protection is still write protection. */
    check(VirtualProtect((void *)region, 4096, 4, &old) && old == 2, 4421);
    rewrite_and_run((unsigned char *)region, 0x7301, 0x7302, 4422);
    check(VirtualFree((void *)region, 0, 0x8000), 4425);

    /* The stack carries code on this machine as well. */
    unsigned char local[16];
    rewrite_and_run(local, 0x7401, 0x7402, 4426);

    /* A second heap block after the first was freed, so a reused block stays
       executable rather than losing the permission with its owner. */
    U again = LocalAlloc(0, 64);
    check(again != 0, 4429);
    rewrite_and_run((unsigned char *)again, 0x7501, 0x7502, 4430);
    check(LocalFree(again) == 0, 4433);
}

void mainCRTStartup(void)
{
    const char mode = *GetCommandLineA();
    if (mode == '1')
    {
        /* Write protection stays fatal: the CPU store into a read-only page
           must stop the process instead of going through. */
        U region = VirtualAlloc(0, 4096, 0x3000, 4), old = 0;
        check(region != 0, 4490);
        check(VirtualProtect((void *)region, 4096, 2, &old), 4491);
        *(volatile U *)region = 98;
        ExitProcess(4492);
    }
    run_executable_pages_test();
    ExitProcess(0);
}
