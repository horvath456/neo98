/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest for the page rights of a mapped image,
   Win98 keeps an image as a copy-on-write view of its file, so every page of
   it is writable; VirtualProtect cannot change its head, but the sections of
   a private module change (Quake II's ref_soft). Exit code zero means
   every check held; codes 4700 to 4799. */
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
/* A const array lands in a section without the write flag. Under Win98 that
   flag never reaches the page table. */
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

    /* Writing into the header region. */
    U *const stamp = (U *)(self + 8);
    const U saved = *stamp;
    *stamp = 0xa5a5a5a5u;
    check(*stamp == 0xa5a5a5a5u, 4707);
    *stamp = saved;
    check(*stamp == saved, 4708);

    /* Writing into a section without the write flag. The reads go through a
       volatile pointer, otherwise the compiler answers them from the constant
       itself and the check would say nothing about the page. */
    volatile U *const constant = (volatile U *)constants;
    check(constant[0] == 0x11223344u, 4709);
    constant[0] = 0x0f0f0f0fu;
    check(constant[0] == 0x0f0f0f0fu, 4710);
    constant[0] = 0x11223344u;
    check(constant[0] == 0x11223344u, 4711);

    /* VirtualProtect cannot change the page rights of the head. */
    U previous = 0;
    SetLastError(0);
    check(VirtualProtect(self, 4096, PAGE_READONLY, &previous) == 0, 4712);
    check(GetLastError() == 487, 4713);
    /* And the page stays writable, which is what the packers rely on. */
    *stamp = 0x5a5a5a5au;
    check(*stamp == 0x5a5a5a5au, 4714);
    *stamp = saved;

    /* Negative probe: on a private reservation VirtualProtect still works and
       still takes the write right away. */
    const U block = VirtualAlloc(0, 8192, MEM_COMMIT, PAGE_READWRITE);
    check(block != 0, 4715);
    if (block)
    {
        *(U *)block = 0x12345678u;
        check(IsBadWritePtr(block, 4) == 0, 4716);
        previous = 0;
        check(VirtualProtect(block, 4096, PAGE_READONLY, &previous) != 0, 4717);
        check(previous == PAGE_READWRITE, 4718);
        check(*(const U *)block == 0x12345678u, 4719);
        check(IsBadWritePtr(block, 4) != 0, 4720);
        check(VirtualProtect(block, 4096, PAGE_READWRITE, &previous) != 0, 4721);
        check(previous == PAGE_READONLY, 4722);
        check(IsBadWritePtr(block, 4) == 0, 4723);
        check(VirtualFree(block, 0, MEM_RELEASE) != 0, 4724);
    }

    /* A section of the private module: VirtualProtect takes it and names the
       rights the loader gave, read and write. */
    previous = 0;
    check(VirtualProtect((U)constants & ~4095u, 4096, PAGE_READWRITE, &previous) != 0, 4725);
    check(previous == PAGE_READWRITE, 4726);
    constant[0] = 0x0f0f0f0fu;
    check(constant[0] == 0x0f0f0f0fu, 4727);
    constant[0] = 0x11223344u;
    ExitProcess(failure);
}
