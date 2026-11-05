// SPDX-License-Identifier: GPL-2.0-or-later
// A program that dies of an unhandled exception, for
// tools/test_fault_box_host.py.
//
// The letter in front of ".EXE" in its own name chooses what it does, so a
// double click needs no arguments:
//
// FAULTP.EXE reads through a null pointer
// FAULTQ.EXE the same after SetErrorMode(SEM_NOGPFAULTERRORBOX)
// FAULTF.EXE the same with a filter of its own that answers
// EXCEPTION_EXECUTE_HANDLER
// FAULTI.EXE runs an opcode the processor does not know (UD2)
// FAULTW.EXE has a window whose procedure reads through a null pointer
// on every message from WM_PAINT on: one fault, one box
//
// FAULTN.EXE asks for an NT service through int 2Eh that the port does
// not carry: the stop of the port ends in the same box
//
//
// Each start first appends its letter to C:\FAULTBOX.TXT; a line "survived"
// would follow if the fault did not end the program.
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API char *GetCommandLineA(void);API U SetErrorMode(U);API U SetUnhandledExceptionFilter(U);
API U CreateFileA(const char*,U,U,void*,U,U,U);API U WriteFile(U,const void*,U,U*,void*);API U CloseHandle(U);
API U SetFilePointer(U,int,int*,U);API void ExitProcess(U);
static void note(const char*s){
 U h=CreateFileA("C:\\FAULTBOX.TXT",0x40000000,3,0,4,0x80,0),n=0,done=0;
 if(h==~0u)return;while(s[n])++n;SetFilePointer(h,0,0,2);WriteFile(h,s,n,&done,0);CloseHandle(h);
}
static U __stdcall filter(void*pointers){(void)pointers;note("filter called\r\n");return 1;}
API U MessageBoxA(U,const char*,const char*,U);
API U CreateThread(void*,U,void*,void*,U,U*); API void Sleep(U); API U PostMessageA(U,U,U,U);
static U __stdcall handler(U record,U frame,U context,U dispatcher){
 (void)record;(void)frame;(void)context;(void)dispatcher;
 MessageBoxA(0,"A live guest exception handler", "SEH handler",0);return 1;
}
static volatile U zero;
API U RegisterClassA(const void*);API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,void*);
API U DefWindowProcA(U,U,U,U);API U GetMessageA(void*,U,U,U);API U DispatchMessageA(const void*);API U GetModuleHandleA(const char*);
static U armed;
static U __stdcall faulting(U h,U m,U w,U l){
 if(m==0xf)armed=1;
 if(armed)zero=*(volatile U*)(U)zero;
 return DefWindowProcA(h,m,w,l);
}
static void window(void){
 U wc[10]={0},message[8];
 wc[1]=(U)faulting;wc[4]=GetModuleHandleA(0);wc[7]=6;wc[9]=(U)"FaultWindow";RegisterClassA(wc);
 CreateWindowExA(0,"FaultWindow","Faulting window",0x10cf0000,40,40,300,200,0,0,wc[4],0);
 while(GetMessageA(message,0,0,0))DispatchMessageA(message);
}
// Another thread of the same process keeps a dispatch callback and modal
// loop open when the main thread faults. It owns its contexts and dialog.
static volatile U worker_ready;
static U __stdcall worker_proc(U h,U m,U w,U l){
 if(m==0x8001){
  worker_ready=1;note("worker callback entered\r\n");
  MessageBoxA(h,"This background callback is still running.","FaultBox worker",0);
  note("worker callback returned\r\n");return 0;
 }
 return DefWindowProcA(h,m,w,l);
}
static U __stdcall worker(void*p){
 (void)p;U wc[10]={0},message[8];
 wc[1]=(U)worker_proc;wc[4]=GetModuleHandleA(0);wc[7]=6;wc[9]=(U)"FaultWorker";RegisterClassA(wc);
 U h=CreateWindowExA(0,"FaultWorker","FaultBox background",0x10cf0000,20,20,280,180,0,0,wc[4],0);
 PostMessageA(h,0x8001,0,0);
 while(GetMessageA(message,0,0,0))DispatchMessageA(message);
 return 0;
}
void mainCRTStartup(void){
 const char*c=GetCommandLineA();char mode[2]={0,0};
 for(;c[0]&&c[1];++c)if(c[1]=='.'&&(c[2]=='E'||c[2]=='e'))mode[0]=(char)(c[0]|32);
 note(mode);note("\r\n");
 if(mode[0]=='q')SetErrorMode(2);
 if(mode[0]=='f')SetUnhandledExceptionFilter((U)filter);
 if(mode[0]=='w')window();
 if(mode[0]=='t'){U id;CreateThread(0,0,worker,0,0,&id);while(!worker_ready)Sleep(1);Sleep(80);}
 if(mode[0]=='e')__asm__ volatile("pushl %0; pushl %%fs:0; movl %%esp, %%fs:0" : : "r"(handler) : "memory");
 if(mode[0]=='n'){__asm__ volatile("int $0x2e"::"a"(0xffffu),"d"(0u):"memory");note("survived\r\n");ExitProcess(0);}
 if(mode[0]=='i')__asm__ volatile("ud2");
 else zero=*(volatile U*)(U)zero;
 note("survived\r\n");ExitProcess(0);
}
