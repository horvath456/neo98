/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest for Win98 image page rights and headers. The measured reference in
   docs/benchmarks/win98-image-protection-2026-10-23.json supersedes the old
   assumption that private image pages are always writable. Headers and
   read-only sections start protected and can be changed by VirtualProtect.
   Exit code zero means every check held; codes 4700 to 4799. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API U GetLastError(void);
API void SetLastError(U);
API U VirtualAlloc(U, U, U, U);
API U VirtualFree(U, U, U);
API U VirtualProtect(U, U, U, U *);
API U IsBadWritePtr(U, U);

#define PAGE_READONLY 2u
#define PAGE_READWRITE 4u
#define MEM_COMMIT 0x1000u
#define MEM_RELEASE 0x8000u

static U failure;
static void check(int ok, U code)
{
    if (!ok && !failure)
        failure = code;
}
/* A const array lands in a section without the write flag. */
static volatile const U constants[4] = {0x11223344u, 0x55667788u, 0x99aabbccu, 0xddeeff00u};

void mainCRTStartup(void)
{
    const U self = GetModuleHandleA(0);
    check(self != 0, 4701);

    /* The header page. The section table of this image may reach past
       SizeOfHeaders; the guest must still find it there. */
    const U nt = *(const U *)(self + 60);
    check(*(const U *)(self + nt) == 0x4550u, 4702);
    const U headers = *(const U *)(self + nt + 24 + 60);
    const U optional = *(const unsigned short *)(self + nt + 20);
    const U sections = *(const unsigned short *)(self + nt + 6);
    const U table = nt + 24 + optional;
    check(sections != 0, 4703);
    check(table + sections * 40 <= 4096u, 4704);
    /* The first section entry has to be readable, whether or not it lies
       above SizeOfHeaders. */
    const U first_rva = *(const U *)(self + table + 12);
    check(first_rva >= 4096u, 4705);
    check(headers != 0, 4706);

    /* Header writes require an explicit protection change. Use volatile so
       the compiler cannot remove the write/read/restore sequence. */
    volatile U *const stamp = (volatile U *)(self + 8);
    const U saved = *stamp;
    U previous = 0;
    check(IsBadWritePtr((U)stamp, 4) != 0, 4707);
    check(VirtualProtect(self, 4096, PAGE_READWRITE, &previous) != 0 &&
          previous == PAGE_READONLY, 4708);
    *stamp = 0xa5a5a5a5u;
    check(*stamp == 0xa5a5a5a5u, 4709);
    *stamp = saved;
    check(*stamp == saved, 4710);
    check(VirtualProtect(self, 4096, PAGE_READONLY, &previous) != 0 &&
          previous == PAGE_READWRITE, 4711);
    check(IsBadWritePtr((U)stamp, 4) != 0, 4712);

    /* The read-only section follows the same protection contract. */
    volatile U *const constant = (volatile U *)constants;
    check(constant[0] == 0x11223344u && IsBadWritePtr((U)constant, 4), 4713);
    check(VirtualProtect((U)constant & ~4095u, 4096, PAGE_READWRITE, &previous) != 0 &&
          previous == PAGE_READONLY, 4714);
    constant[0] = 0x0f0f0f0fu;
    check(constant[0] == 0x0f0f0f0fu, 4715);
    constant[0] = 0x11223344u;
    check(constant[0] == 0x11223344u, 4716);
    check(VirtualProtect((U)constant & ~4095u, 4096, PAGE_READONLY, &previous) != 0 &&
          previous == PAGE_READWRITE, 4717);
    check(IsBadWritePtr((U)constant, 4) != 0, 4718);

    /* A private reservation is writable initially. Protecting its first page
       must leave the second page writable. */
    const U block = VirtualAlloc(0, 8192, MEM_COMMIT, PAGE_READWRITE);
    check(block != 0, 4720);
    if (block)
    {
        *(volatile U *)block = 0x12345678u;
        check(IsBadWritePtr(block, 4) == 0, 4721);
        check(VirtualProtect(block, 4096, PAGE_READONLY, &previous) != 0, 4722);
        check(previous == PAGE_READWRITE, 4723);
        check(*(volatile U *)block == 0x12345678u, 4724);
        check(IsBadWritePtr(block, 4) != 0, 4725);
        check(IsBadWritePtr(block + 4096, 4) == 0, 4726);
        check(VirtualProtect(block, 4096, PAGE_READWRITE, &previous) != 0, 4727);
        check(previous == PAGE_READONLY, 4728);
        check(IsBadWritePtr(block, 4) == 0, 4729);
        check(VirtualFree(block, 0, MEM_RELEASE) != 0, 4730);
    }
    ExitProcess(failure);
}
