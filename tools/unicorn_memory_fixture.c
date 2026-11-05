// SPDX-License-Identifier: GPL-2.0-or-later
// Guest writes over the memory paths under test: the slow store
// path of Unicorn, the invalidation of translated code and the cache in front
// of the mapping lookup. The program writes into a page it has already executed
// from, maps and unmaps memory so that the same address belongs to a different
// region, and reads back what it wrote each time.
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U GetProcessHeap(void);
API U HeapAlloc(U, U, U);
API U HeapFree(U, U, U);
API U VirtualAlloc(U, U, U, U);
API U VirtualFree(U, U, U);
API U VirtualProtect(U, U, U, U *);
API void ExitProcess(U);
static void check(U value, U code)
{
    if (!value)
        ExitProcess(code);
}
typedef U(__stdcall *Entry)(void);
// mov eax, <value>; ret. The value sits at offset one, so the next writer only
// changes those four bytes of a page it has already run code from.
static Entry build_constant(U block, U value)
{
    unsigned char *p = (unsigned char *)block;
    p[0] = 0xb8;
    p[1] = (unsigned char)value;
    p[2] = (unsigned char)(value >> 8);
    p[3] = (unsigned char)(value >> 16);
    p[4] = (unsigned char)(value >> 24);
    p[5] = 0xc3;
    return (Entry)block;
}
void mainCRTStartup(void)
{
    U heap = GetProcessHeap();
    check(heap, 19200);
    U block = HeapAlloc(heap, 8, 4096);
    check(block, 19201);
    // Self modifying code. Every round writes into a page that already carries
    // a translated block, so every round takes the slow store path and has to
    // throw the old translation away. Sixty four rounds are more than the
    // threshold at which Unicorn starts keeping a bitmap of the code bytes of
    // the page, so both branches of that decision run here.
    for (U i = 0; i < 64; ++i)
        check(build_constant(block, 0x51000000u + i)() == 0x51000000u + i, 19210);
    // A second entry point in the same page. The first one has to keep
    // answering after the second one was written and run.
    Entry first = build_constant(block, 0x52000000u);
    Entry second = build_constant(block + 64, 0x53000000u);
    check(first() == 0x52000000u, 19211);
    check(second() == 0x53000000u, 19212);
    check(first() == 0x52000000u, 19213);
    // Data writes into the same page as the code above.
    volatile U *data = (volatile U *)(block + 2048);
    for (U i = 0; i < 4096; ++i)
        data[i & 255] = i;
    // The last round of the loop wrote 3840 to index 0 and 4095 to index 255.
    check(data[255] == 4095, 19214);
    check(data[0] == 3840, 19215);
    check(first() == 0x52000000u, 19216);
    check(HeapFree(heap, 0, block), 19217);
    // A page whose last translated block is gone leaves the slow store path
    // (Unicorn's codeclean group), and the next block that
    // reaches into it must put it back. The block here starts two bytes before
    // a page boundary, so its immediate ends in the second page, and that page
    // has just lost its only block of its own.
    U span = HeapAlloc(heap, 8, 3 * 4096);
    check(span, 19230);
    U boundary = (span + 4096) & ~4095u;
    check(build_constant(boundary + 256, 0x55000000u)() == 0x55000000u, 19231);
    *(volatile unsigned char *)(boundary + 256) = 0xc3; // throws that block away
    for (U i = 0; i < 64; ++i)
        ((volatile U *)(boundary + 1024))[i] = i; // the page is clean now
    Entry straddle = build_constant(boundary - 2, 0x56000000u);
    check(straddle() == 0x56000000u, 19232);
    // The top byte of the immediate lies in the second page.
    *(volatile unsigned char *)(boundary + 2) = 0x57;
    check(straddle() == 0x57000000u, 19233);
    *(volatile unsigned char *)(boundary - 1) = 0x01; // and the low byte in the first
    check(straddle() == 0x57000001u, 19234);
    check(HeapFree(heap, 0, span), 19235);
    // Map, write, unmap, map again. Unicorn answers the question which region
    // an address belongs to from a cache, and this is the sequence that must
    // empty it: the second reservation may well get the address of the first.
    for (U round = 0; round < 32; ++round) {
        U region = VirtualAlloc(0, 0x10000, 0x1000 | 0x2000, 0x40); // COMMIT|RESERVE, EXECUTE_READWRITE
        check(region, 19220);
        volatile U *cell = (volatile U *)region;
        cell[0] = 0x60000000u + round;
        cell[1023] = 0x70000000u + round;
        check(cell[0] == 0x60000000u + round, 19221);
        check(cell[1023] == 0x70000000u + round, 19222);
        // Code in a freshly mapped region, so the fetch and the following
        // writes meet a region the cache has only just learned about.
        check(build_constant(region + 8192, 0x54000000u + round)() == 0x54000000u + round, 19223);
        U old = 0;
        check(VirtualProtect(region, 4096, 0x02, &old), 19224); // READONLY
        check(VirtualProtect(region, 4096, 0x40, &old), 19225); // EXECUTE_READWRITE
        cell[0] = 0x61000000u + round;
        check(cell[0] == 0x61000000u + round, 19226);
        check(VirtualFree(region, 0, 0x8000), 19227); // MEM_RELEASE
    }
    // a large executable allocation and repeated protection changes
    // must not fragment Unicorn's map once for every 4 KiB page.
    U large = VirtualAlloc(0, 32 * 1024 * 1024, 0x3000, 0x40);
    check(large, 19240);
    U tail = large + 32 * 1024 * 1024 - 4096;
    check(build_constant(large, 0x12345678)() == 0x12345678, 19241);
    check(build_constant(tail, 0x76543210)() == 0x76543210, 19242);
    U old = 0;
    for (U i = 0; i < 3; ++i) {
        check(VirtualProtect(large, 32 * 1024 * 1024, 0x02, &old), 19243);
        check(((Entry)large)() == 0x12345678, 19244);
        check(((Entry)tail)() == 0x76543210, 19245);
        check(VirtualProtect(large, 32 * 1024 * 1024, 0x40, &old), 19246);
        check(build_constant(tail, 0x76543211)() == 0x76543211, 19247);
        check(build_constant(tail, 0x76543210)() == 0x76543210, 19248);
    }
    check(VirtualFree(large, 0, 0x8000), 19249);
    ExitProcess(0);
}
