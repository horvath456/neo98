/* SPDX-License-Identifier: GPL-2.0-or-later */
/* reuse the same reserved page and faulting instruction 32 times. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API void *VirtualAlloc(void *, U, U, U);
API U VirtualFree(void *, U, U);
struct Record { U code, flags, previous, address, count, args[15]; };
struct Frame { struct Frame *next; void *handler; };
static volatile U *page;
static U calls, instruction;
static void check(U yes, U code) { if (!yes) ExitProcess(code); }
static int repair(struct Record *r, struct Frame *f, U *c, U *d)
{
    (void)f; (void)d;
    if (r->code != 0xc0000005) return 1;
    check(r->count == 2 && r->args[0] == 0 && r->args[1] == (U)page, 10);
    if (!calls) instruction = c[46];
    check(instruction == c[46] && r->address == instruction, 11);
    check(VirtualAlloc((void *)page, 4096, 0x1000, 4) == (void *)page, 12);
    *page = 0x12340000 + ++calls;
    return 0;
}
__attribute__((noinline)) static U read_page(void) { return *page; }
void mainCRTStartup(void)
{
    struct Frame frame;
    __asm__ volatile("movl %%fs:0,%0" : "=r"(frame.next));
    frame.handler = repair;
    __asm__ volatile("movl %0,%%fs:0" :: "r"(&frame) : "memory");
    page = VirtualAlloc(0, 65536, 0x2000, 4);
    check(page != 0, 20);
    for (U i = 1; i <= 32; ++i) {
        check(read_page() == 0x12340000 + i && calls == i, 21);
        check(VirtualFree((void *)page, 0, 0x8000), 22);
        if (i < 32)
            check(VirtualAlloc((void *)page, 65536, 0x2000, 4) == (void *)page, 23);
    }
    __asm__ volatile("movl %0,%%fs:0" :: "r"(frame.next) : "memory");
    ExitProcess(0);
}
