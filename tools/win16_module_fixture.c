// SPDX-License-Identifier: GPL-2.0-or-later
#include "win16_module_imports.h"
#define P(x) ((U)(x))
#define CHECK(x) do{if(!(x))ExitProcess(__LINE__);}while(0)
U __stdcall call16(U,U,U,U);
U __stdcall stack_pointer(void);
__asm__(".text\n.globl _stack_pointer@0\n_stack_pointer@0:\n mov %esp,%eax\n ret\n");
__asm__(".text\n.globl _call16@16\n_call16@16:\n"
 " push %ebp\n mov %esp,%ebp\n sub $64,%esp\n mov 8(%ebp),%edx\n"
 " pushl 12(%ebp)\n pushl 16(%ebp)\n pushl 20(%ebp)\n call *__imp__QT_Thunk@0\n"
 " shll $16,%eax\n shrdl $16,%edx,%eax\n leave\n ret $16\n");
static volatile U entered;
static U __stdcall level_worker(U lock){
 EnterSysLevel(lock);CHECK(ConfirmWin16Lock()==1);entered=1;LeaveSysLevel(lock);return 0;
}
static U __stdcall thunk_worker(U unused){
 // PLAN.md step 8: no global Win16 lock guards a thunk transition any more,
 // so this thread runs the counter path while the first thread holds the
 // system level of GetpWin16Lock.
 U count=7;(void)unused;
 CHECK(RestoreThunkLock(3)==GetCurrentThreadId());
 CHECK(ReleaseThunkLock(P(&count))==P(&count)&&count==3);
 CHECK(!ConfirmWin16Lock());
 entered=1;return 0;
}
static void lock_word_test(void){
 U lock=0,count=7;GetpWin16Lock(P(&lock));CHECK(lock);
 // The word at fs:[0x1e] is a counter. Reading it out signs it and leaves
 // 0xffff behind; nothing is acquired or released.
 CHECK(!ConfirmWin16Lock());
 CHECK(RestoreThunkLock(0xffff)==GetCurrentThreadId()&&!ConfirmWin16Lock());
 CHECK(ReleaseThunkLock(P(&count))==P(&count)&&count==0xffffffffu);
 CHECK(RestoreThunkLock(5)==GetCurrentThreadId());
 CHECK(ReleaseThunkLock(P(&count))==P(&count)&&count==5);
 CHECK(ReleaseThunkLock(P(&count))==P(&count)&&count==0xffffffffu);
 // The record of GetpWin16Lock is an ordinary recursive system level.
 EnterSysLevel(lock);CHECK(ConfirmWin16Lock()==1);
 EnterSysLevel(lock);CHECK(ConfirmWin16Lock()==2);
 LeaveSysLevel(lock);CHECK(ConfirmWin16Lock()==1);
 entered=0;U thread=CreateThread(0,0,P(thunk_worker),0,0,0);CHECK(thread);
 CHECK(WaitForSingleObject(thread,1000)==0&&entered);CloseHandle(thread);
 // Only an explicit EnterSysLevel on the same record waits.
 entered=0;thread=CreateThread(0,0,P(level_worker),lock,0,0);CHECK(thread);
 CHECK(WaitForSingleObject(thread,5)==258&&!entered&&ConfirmWin16Lock()==1);
 LeaveSysLevel(lock);CHECK(WaitForSingleObject(thread,1000)==0&&entered);CloseHandle(thread);
 CHECK(!ConfirmWin16Lock());
}
void mainCRTStartup(void){
 lock_word_test();
 CHECK(LoadLibrary16(P("C:\\not-here\\SERIALUI.DLL"))==2);
 CHECK(LoadLibrary16(P("KERNEL32.DLL"))==21);
 U module=LoadLibrary16(P("serialui"));CHECK(module>=32&&module<=65535);
 CHECK(LoadLibrary16(P("C:\\WINDOWS\\SYSTEM\\SERIALUI.DLL"))==module);
 U get=GetProcAddress16(module,P("drvGetDefaultCommConfig")),set=GetProcAddress16(module,4);
 CHECK(get&&set&&get==GetProcAddress16(module,5)&&!GetProcAddress16(module,200)&&!GetProcAddress16(module,P("missing")));
 FreeLibrary16(module);CHECK(GetProcAddress16(module,5)==get);
 U cc[13]={0},n=52,name=MapLS(P("COM1")),config=MapLS(P(cc)),size=MapLS(P(&n));
 CHECK(name&&config&&size);CHECK(call16(get,name,config,size)==0&&n==52&&cc[0]==52&&cc[2]==28);
 cc[3]=38400;CHECK(call16(set,name,config,52)==0);cc[3]=0;
 CHECK(call16(get,name,config,size)==0&&cc[3]==38400);
 n=51;CHECK(call16(get,name,config,size)==122&&n==52);
 U flat=MapHModuleSL(module);CHECK(flat>=65536&&MapHModuleLS(flat)==module);
 UnMapLS(name);UnMapLS(config);UnMapLS(size);
 FreeLibrary16(module);CHECK(!GetProcAddress16(module,5)&&!MapHModuleSL(module));
 U next=LoadLibrary16(P("SERIALUI.DLL"));CHECK(next>=32&&next!=module);FreeLibrary16(next);
 // MAIN.CPL takes every answer of LoadLibrary16 above zero for a
 // module and then calls GetProcAddress16 and FreeLibrary16 on it. KERNEL32
 // ordinal 36 is FreeLibrary16 with one argument and ordinal 37 is
 // GetProcAddress16 with two; a gateway that swaps the two leaves the caller
 // with a stack pointer four bytes too high and returns into a data word.
 U mark=stack_pointer();
 CHECK(LoadLibrary16(P("C:\\not-here\\MOUSE.DRV"))==2);
 CHECK(!GetProcAddress16(2,P("MouseProc")));
 FreeLibrary16(2);
 CHECK(stack_pointer()==mark);
 ExitProcess(0);
}
