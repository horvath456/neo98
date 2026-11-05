/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
struct Record { U code,flags,previous,address,count,args[15]; };
struct Registration { struct Registration *next; void *handler; };
static volatile U calls, expected;
volatile U fault_ip, resume_ip;
static U divisor;
static int handler(struct Record *r, struct Registration *f, U *c, U *d) {
    (void)f;(void)d;
    if(r->code!=0xc0000094) ExitProcess(1);
    if(r->address!=fault_ip || c[46]!=fault_ip || r->count || (r->flags&1)) ExitProcess(2);
    if(c[44]!=10 || c[42]!=0 || c[43]!=expected) ExitProcess(3); /* EAX, EDX, ECX unchanged */
    ++calls;c[46]=resume_ip;return 0;
}
#define PROBE(instruction,value) do { \
    U result;expected=(value); \
    __asm__ volatile( \
        "movl $1f,_fault_ip\n\tmovl $2f,_resume_ip\n\t" \
        "movl $10,%%eax\n\txorl %%edx,%%edx\n\t" \
        "1: " instruction "\n\t2: movl %%eax,%0" \
        : "=r"(result) : "c"(value), "S"(&divisor) : "eax","edx","memory","cc"); \
    if(result!=10) ExitProcess(4); \
} while(0)
#define NONZERO(instruction) do { \
    U result; \
    __asm__ volatile("movl $10,%%eax\n\txorl %%edx,%%edx\n\t" instruction "\n\tmovl %%eax,%0" \
        : "=r"(result) : "c"(2) : "eax","edx","memory","cc"); \
    ++expected; if(result!=5) ExitProcess(20+expected); \
} while(0)
void mainCRTStartup(void) {
    struct Registration frame;
    __asm__ volatile("movl %%fs:0,%0":"=r"(frame.next));frame.handler=handler;
    __asm__ volatile("movl %0,%%fs:0"::"r"(&frame):"memory");
    PROBE("divb %%cl",0x12340000); PROBE("divw %%cx",0x12340000); PROBE("divl %%ecx",0);
    PROBE("idivb %%cl",0x12340000); PROBE("idivw %%cx",0x12340000); PROBE("idivl %%ecx",0);
    PROBE("divb (%%esi)",0); PROBE("divw (%%esi)",0); PROBE("divl (%%esi)",0);
    PROBE("idivb (%%esi)",0); PROBE("idivw (%%esi)",0); PROBE("idivl (%%esi)",0);
    expected=0;
    NONZERO("divb %%cl");NONZERO("divw %%cx");NONZERO("divl %%ecx");
    NONZERO("idivb %%cl");NONZERO("idivw %%cx");NONZERO("idivl %%ecx");
    __asm__ volatile("movl %0,%%fs:0"::"r"(frame.next):"memory");
    if(calls!=12) ExitProcess(6);
    ExitProcess(0);
}
