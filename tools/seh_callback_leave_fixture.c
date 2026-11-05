/* SPDX-License-Identifier: GPL-2.0-or-later */
/* a handler that RaiseException called, and that leaves through
   RtlUnwind and a non-local jump, inside a callback of the port.

   The Web Folders extension MSONSEXT.DLL does exactly this when Explorer shows
   the Recycle Bin: its delay-load helper raises C06D007E for the missing
   Msi.dll, the __except block of the caller catches it, and the window thread
   goes on. Win98 leaves the kernel frames of the dispatch on the stack the
   handler jumped over. This port runs the handler as a callback loop; if that
   loop stays open, the next return of the enclosing callback lands in it,
   the loop takes it for the handler's own return and puts back the registers
   of the RaiseException call. The guest then continues on a stack that has
   been overwritten since, which is how Explorer came to return to text of
   "C:\WINDOWS\History".

   CallWindowProcA calls raise_window as a callback of the port, forty rounds,
   more than the nesting limit of 32. Each round raises, the handler unwinds
   and jumps back behind RaiseException, and the window procedure returns a
   value of its own that the caller checks. The chain after the jump is the one
   RtlUnwind left: the catching frame on top, not the frame it removed. */
#include "kernel_exceptions_fixture.c"
API U CallWindowProcA(void*,U,U,U,U);
extern U seh_raise_round(void);extern void seh_raise_longjmp(void);U seh_raise_state[2];
static U raise_calls;
static int raise_leap(struct Record*r,struct Registration*f,U*c,U*d){
 (void)c;(void)d;
 if(r->code!=0xe1230010)return 1;
 ++raise_calls;
 RtlUnwind(f,0,0,0);
 seh_raise_longjmp();
 return 22851; /* not reached */
}
/* Between the raise and the catching frame lies a frame that only passes the
   search on; the unwind of the catching handler removes it. */
static U pass_calls;
static int raise_pass(struct Record*r,struct Registration*f,U*c,U*d){(void)f;(void)c;(void)d;if(!(r->flags&6))++pass_calls;return 1;}
/* The passing frame lies deeper on the stack than the catching one, as the
   chain requires; after the jump it is gone from the chain. */
static U __attribute__((noinline)) pass_round(void){
 struct Registration h;link(&h,raise_pass);
 return seh_raise_round();
}
static U __stdcall raise_window(U hwnd,U message,U wp,U lp){
 (void)hwnd;(void)message;(void)lp;
 struct Registration g;U before=head();link(&g,raise_leap);
 U code=pass_round();
 check(head()==(U)&g,22852);unlink(&g);check(head()==before,22853);
 return code?code:0x5eb00000u+wp;
}
/* forty raises inside one call of a window procedure, which does not
   return between them. Every handler has to give its level back by itself. */
static U __stdcall many_window(U hwnd,U message,U wp,U lp){
 (void)hwnd;(void)message;(void)wp;(void)lp;
 for(U i=0;i<40;++i){U code=raise_window(0,0,i,0);if(code!=0x5eb00000u+i)return code;}
 return 0x5eb0cafe;
}
/* an outer handler returns normally after a nested handler unwound
   and jumped back into it. Its disposition and context still belong to the
   outer RaiseException, even though the nested callback was abandoned. */
static U nested_returns;
static int nested_return(struct Record*r,struct Registration*f,U*c,U*d){
 (void)f;(void)c;(void)d;
 if(r->flags&6 || r->code!=0xe1230020)return 1;
 struct Registration inner;U previous=head();link(&inner,raise_leap);
 U result=seh_raise_round();
 check(!result&&head()==(U)&inner,22860);unlink(&inner);check(head()==previous,22861);
 ++nested_returns;
 return 0;
}
void mainCRTStartup(void){
 const char*command=GetCommandLineA();for(;*command;++command)if(*command=='!'){RaiseException(0xe123dead,0,0,0);ExitProcess(22899);}
 U previous=head();
 for(U i=0;i<40;++i){U value=CallWindowProcA(raise_window,0,0x400,i,0);check(value==0x5eb00000u+i,value<0x10000?value:22854);}
 check(raise_calls==40&&pass_calls==40,22855);check(head()==previous,22856);
 U many=CallWindowProcA(many_window,0,0x400,0,0);check(many==0x5eb0cafe,many<0x10000?many:22857);
 check(raise_calls==80&&pass_calls==80,22858);check(head()==previous,22859);
 struct Registration nested;link(&nested,nested_return);
 for(U i=0;i<40;++i)RaiseException(0xe1230020,0,0,0);
 unlink(&nested);check(head()==previous,22862);
 check(nested_returns==40&&raise_calls==120,22863);
 /* Negative probe: an ordinary handler of a raise inside a callback still
    returns its disposition and the raise returns to its caller. */
 run_kernel_exceptions_test();
 ExitProcess(0);
}
