/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API void RaiseException(U,U,U,const U*);API U SetUnhandledExceptionFilter(void*);API int UnhandledExceptionFilter(void*);API U SetErrorMode(U);API const char*GetCommandLineA(void);API void RtlUnwind(void*,void*,void*,U);
struct Record{U code,flags,previous,address,count,args[15];};struct Registration{struct Registration*next;void*handler;};
static void check(int yes,U code){if(!yes)ExitProcess(code);}
static U head(void){U p;__asm__ volatile("movl %%fs:0,%0":"=r"(p));return p;}
static void link(struct Registration*r,void*handler){r->next=(void*)head();r->handler=handler;__asm__ volatile("movl %0,%%fs:0"::"r"(r):"memory");}
static void unlink(struct Registration*r){__asm__ volatile("movl %0,%%fs:0"::"r"(r->next):"memory");}
static U inner_calls,outer_calls,secondary,filter_calls;U seh_expected_sp;
extern U seh_register_probe(void);extern char seh_resume_return,seh_resume_target;
/* The handler that does not return. seh_leap_state holds the ESP and
   EBP of the round; seh_leap_longjmp puts them back and continues the round
   behind its fault, the way longjmp out of a handler does. */
extern U seh_leap_round(void);extern void seh_leap_longjmp(void);U seh_leap_state[2];
static U leap_calls;
static int __stdcall filter(void*p){U*v=p;struct Record*r=(void*)v[0];check(r->code==0x1234&&v[1],22730);++filter_calls;return -1;}
static int outer(struct Record*r,struct Registration*f,U*c,U*d){
 (void)f;(void)d;++outer_calls;check(c[0]==0x10007,22701);
 if(r->code==0xe1230001){check(r->count==15&&r->args[0]==0x1100&&r->args[14]==0x110e,22702);return 0;}
 if(r->code==0xe1230002){check(!(r->flags&16),22703);++secondary;return 0;}
 if(r->code==0xe1230003){check(c[46]==(U)&seh_resume_return&&r->address==c[46]&&c[49]==seh_expected_sp,22704);c[44]=0xabcdef01;c[41]=0x23456789;c[46]=(U)&seh_resume_target;return 0;}
 if(r->code==0xe1230004){check(r->flags&1,22705);r->flags&=~1u;return 0;}
 return 1;
}
/* A page fault and a breakpoint of the guest are exceptions of the process,
   not a stop of the emulator: both visit this chain. The handler repairs the
   read by answering a value in EAX and continuing behind the instruction. */
extern char fault_resume;
static U fault_calls,break_calls;
static int fault(struct Record*r,struct Registration*f,U*c,U*d){
 (void)f;(void)d;
 if(r->code==0xc0000005){
  ++fault_calls;
  check(c[0]==0x10007&&r->count==2&&r->args[0]==0&&r->args[1]==0,22740);
  check(r->address==c[46]&&!(r->flags&1),22741);
  c[44]=0x5678;c[46]=(U)&fault_resume;return 0;
 }
 if(r->code==0x80000003){++break_calls;check(r->count==0,22742);return 0;}
 return 1;
}
static U __attribute__((noinline)) read_null(void){
 U value=1;
 __asm__ volatile("xorl %%eax,%%eax\n\tmovl (%%eax),%%eax\n\t.globl _fault_resume\n_fault_resume:\n\tmovl %%eax,%0"
                  :"=r"(value)::"eax","memory");
 return value;
}
/* Win98 unwinds the frames below the handler's own and then continues on the
   stack of that frame. The dispatcher frame of the kernel is left behind; a
   port that runs a callback as a loop of its own has to leave it behind too,
   otherwise every round keeps one callback level. */
static int leap(struct Record*r,struct Registration*f,U*c,U*d){
 (void)c;(void)d;
 if(r->code!=0xc0000005)return 1;
 ++leap_calls;
 RtlUnwind(f,0,0,0);
 seh_leap_longjmp();
 return 22751; /* not reached */
}
static int inner(struct Record*r,struct Registration*f,U*c,U*d){
 (void)f;(void)c;(void)d;++inner_calls;
 if(r->code==0xe1230001){U before=head();RaiseException(0xe1230002,0,0,0);check(head()==before,22706);}
 if(r->code==0xe1230002)check(r->flags&16,22707);
 return 1;
}
void run_kernel_exceptions_test(void){
 const char*command=GetCommandLineA();for(;*command;++command)if(*command=='!'){RaiseException(0xe123dead,0,0,0);ExitProcess(22799);}
 struct Registration a,b;U previous=head();link(&a,outer);link(&b,inner);U arguments[20];for(U i=0;i<20;++i)arguments[i]=0x1100+i;
 RaiseException(0xe1230001,0,20,arguments);check(inner_calls==2&&outer_calls==2&&secondary==1&&head()==(U)&b,22708);
 check(seh_register_probe()==0,22709);check(head()==(U)&b,22710);
 RaiseException(0xe1230004,1,0,0);check(head()==(U)&b,22711);
 unlink(&b);unlink(&a);check(head()==previous,22712);
 check(!SetUnhandledExceptionFilter(filter),22713);U context[51]={0x10007};struct Record r={0x1234};U pointers[2]={(U)&r,(U)context};check(UnhandledExceptionFilter(pointers)==-1&&filter_calls==1,22714);check(SetUnhandledExceptionFilter(0)==(U)filter,22715);
 U old=SetErrorMode(2);check(UnhandledExceptionFilter(pointers)==1,22716);SetErrorMode(old);
 struct Registration f;link(&f,fault);
 check(read_null()==0x5678&&fault_calls==1,22743);
 __asm__ volatile("int3");
 check(break_calls==1,22744);
 /* Sixty-four rounds, twice the nesting limit of the port. Each round leaves
    its handler without returning. The negative probe follows: the ordinary
    handler below still repairs a fault and returns, so the accounting of the
    abandoned frames left the chain alone. */
 struct Registration g;link(&g,leap);
 for(U i=0;i<40;++i){U code=seh_leap_round();check(code==0,code?code:22752);}
 check(leap_calls==40,22753);
 unlink(&g);check(head()==(U)&f,22754);
 check(read_null()==0x5678&&fault_calls==2,22755);
 unlink(&f);check(head()==previous,22745);
}
#ifdef KERNEL_EXCEPTIONS_STANDALONE
void mainCRTStartup(void){run_kernel_exceptions_test();ExitProcess(0);}
#endif
