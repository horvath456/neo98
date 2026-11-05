/* SPDX-License-Identifier: GPL-2.0-or-later */
/* the Win32 system BIOS window, independent of virtual allocations. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U VirtualQuery(const void *, void *, U);
API void ExitProcess(U);
struct Record { U code, flags, previous, address, count, args[15]; };
struct Frame { struct Frame *next; void *handler; };
extern char bios_after, null_after;
static U resume, faults, exception, fault_address;
static volatile U checksum;
static int handler(struct Record *r, struct Frame *f, U *context, U *dispatch)
{
    (void)f; (void)dispatch;
    ++faults; exception = r->code; fault_address = r->args[1];
    context[46] = resume;
    return 0;
}
void mainCRTStartup(void)
{
    struct Frame frame;
    __asm__ volatile("movl %%fs:0,%0" : "=r"(frame.next));
    frame.handler = handler;
    __asm__ volatile("movl %0,%%fs:0" :: "r"(&frame) : "memory");
    resume = (U)&bios_after;
    __asm__ volatile("movl 0xf0000,%%eax\n.globl _bios_after\n_bios_after:" ::: "eax", "memory");
    if (faults) ExitProcess(1);
    /* The complete range searched by 3DMark, including page boundaries. */
    volatile unsigned char *rom = (volatile unsigned char *)0xf0000;
    for (U i = 0; i < 65536; ++i) checksum += rom[i];
    /* Win98 permits the store; whether different data sticks is a chipset
       property, so preserve the firmware contents in this access test. */
    rom[0] = rom[0];
    const char date[] = "04/23/99";
    for (U i = 0; i < 8; ++i) if (rom[0xfff5 + i] != date[i]) ExitProcess(2);
    if (rom[0xfffe] != 0xfc || rom[0xffff] != 0) ExitProcess(3);
    U info[7];
    if (VirtualQuery((void *)0xf0000, info, sizeof info) != sizeof info ||
        info[0] != 0xf0000 || info[4] != 0x10000 || info[5] != 0) ExitProcess(4);
    resume = (U)&null_after;
    __asm__ volatile("movl 0,%%eax\n.globl _null_after\n_null_after:" ::: "eax", "memory");
    if (faults != 1 || exception != 0xc0000005 || fault_address != 0) ExitProcess(5);
    __asm__ volatile("movl %0,%%fs:0" :: "r"(frame.next) : "memory");
    ExitProcess(0);
}
