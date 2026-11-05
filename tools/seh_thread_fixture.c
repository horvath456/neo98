/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A handler may yield while another thread executes on a higher stack. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U); API void RaiseException(U,U,U,const U*); API void Sleep(U);
API U CreateThread(void*,U,U (__stdcall *)(void*),void*,U,U*);
API U WaitForSingleObject(U,U); API U GetExitCodeThread(U,U*); API U CloseHandle(U);
struct Record { U code, flags, previous, address, count, args[15]; };
struct Frame { struct Frame *next; void *handler; };
static volatile U entered, done, rounds;
static U __stdcall worker(void *unused) {
 (void)unused;
 while(!done) { if(entered) for(U i=0;i<20000;i++) ++rounds; Sleep(1); }
 return 73;
}
static int handler(struct Record*r,struct Frame*f,U*c,U*d) {
 (void)f;(void)c;(void)d;
 if(r->code!=0xe1236275) return 1;
 U before=rounds; entered=1; Sleep(30); entered=0;
 if(rounds==before) ExitProcess(21);
 return 0;
}
void mainCRTStartup(void) {
 U thread=CreateThread(0,0,worker,0,0,0),code=0;
 if(!thread) ExitProcess(20);
 struct Frame f; __asm__ volatile("movl %%fs:0,%0":"=r"(f.next)); f.handler=handler;
 __asm__ volatile("movl %0,%%fs:0"::"r"(&f):"memory");
 for(U i=0;i<40;i++) RaiseException(0xe1236275,0,0,0);
 __asm__ volatile("movl %0,%%fs:0"::"r"(f.next):"memory");
 done=1;
 if(WaitForSingleObject(thread,5000)||!GetExitCodeThread(thread,&code)||code!=73) ExitProcess(22);
 CloseHandle(thread);ExitProcess(0);
}
