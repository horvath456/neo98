// SPDX-License-Identifier: GPL-2.0-or-later
// Expected bits/endptr/errno copied from the focused original MSVCRT corpus.
typedef unsigned U;typedef unsigned long long Q;
#define API __declspec(dllimport) __stdcall
#define CRT __declspec(dllimport) __cdecl
API U CreateThread(void*,U,U(__stdcall*)(void*),void*,U,U*);API U WaitForSingleObject(U,U);API U GetExitCodeThread(U,U*);API U CloseHandle(U);API void Sleep(U);
API void ExitProcess(U);API U GetModuleHandleA(const char*);API U GetProcAddress(U,const char*);
CRT double strtod(const char*,char**);CRT double atof(const char*);CRT int*_errno(void);CRT char*setlocale(int,const char*);
typedef union{double value;Q bits;} Bits;
static void check(U okay,U error){if(!okay)ExitProcess(error);}
static const struct{const char*text;Q strtod_bits,atof_bits;U end;int error;} cases[]={
 {"",0x0000000000000000ull,0x0000000000000000ull,0,77},
 {"  -1.25tail",0xbff4000000000000ull,0xbff4000000000000ull,7,77},
 {"1e+",0x3ff0000000000000ull,0x3ff0000000000000ull,1,77},
 {"1D2",0x4059000000000000ull,0x4059000000000000ull,3,77},
 {"0x10",0x0000000000000000ull,0x0000000000000000ull,1,77},
 {"-0",0x8000000000000000ull,0x8000000000000000ull,2,77},
 {"1e309",0x7ff0000000000000ull,0x7ff0000000000000ull,5,34},
 {"-1e5201",0xfff0000000000000ull,0xfff0000000000000ull,7,34},
 {"-1e-309",0x0000000000000000ull,0x8000b8157268fdaeull,7,34},
 {"4.9406564584124654e-324",0x0000000000000000ull,0x0000000000000002ull,23,34},
 {"9007199254740993",0x4340000000000000ull,0x4340000000000000ull,16,77},
 {"1.000000000000000111022302462515654",0x3ff0000000000000ull,0x3ff0000000000000ull,35,77},
 {"9999999999999999999999999",0x45208b2a2c280291ull,0x45208b2a2c280291ull,25,77},
};
static volatile U thread_phase;
static U __stdcall floating_worker(void*parameter){
 U environment[7];
 __asm__ volatile("fnstenv %0\n\tfldenv %0":"=m"(environment));
 // A fresh Win32 thread: control word 027Fh as KERNEL32's x87 emulator sets
 // it, zero status, all tags empty.
 check((environment[0]&65535)==0x27f,24570);
 check((environment[1]&65535)==0&&(environment[2]&65535)==0xffff,24571);
 check((U)parameter==98,24572);
 Bits converted;char*end=0;converted.value=strtod("-12.5tail",&end);
 check(converted.bits==0xc029000000000000ull&&*end=='t',24573);
 unsigned short control=0xb7f,actual=0;Bits held;held.bits=0x400a000000000000ull;
 __asm__ volatile("fldcw %0\n\tfldl %1"::"m"(control),"m"(held.value));
 thread_phase=1;Sleep(0);Sleep(0);
 __asm__ volatile("fnstcw %0\n\tfstpl %1":"=m"(actual),"=m"(held.value));
 check(actual==control&&held.bits==0x400a000000000000ull,24574);
 thread_phase=2;return 98;
}
static void thread_floating_state(void){
 unsigned short before=0,control=0x77f,actual=0;Bits held;U id=0,exitcode=0;
 __asm__ volatile("fnstcw %0":"=m"(before));
 __asm__ volatile("fldcw %0\n\tfld1"::"m"(control));
 U child=CreateThread(0,0,floating_worker,(void*)98,0,&id);check(child&&id,24575);
 U environment[7];__asm__ volatile("fnstenv %0\n\tfldenv %0":"=m"(environment));
 check((environment[0]&65535)==control&&((environment[1]>>11)&7)==7,24576);
 check((environment[2]&65535)==0x3fff,24577); // Physical slot7 contains parent's1.
 check(WaitForSingleObject(child,0xffffffff)==0,24578);
 check(thread_phase==2&&GetExitCodeThread(child,&exitcode)&&exitcode==98,24579);
 __asm__ volatile("fnstcw %0\n\tfstpl %1":"=m"(actual),"=m"(held.value));
 check(actual==control&&held.bits==0x3ff0000000000000ull,24580);
 __asm__ volatile("fldcw %0"::"m"(before));check(CloseHandle(child),24581);
}
void run_crt_strtod_test(void){
 check(setlocale(0,"C")!=0,24400);int*error=_errno();check(error!=0,24401);
 for(U i=0;i<sizeof cases/sizeof cases[0];++i){char*end=0;*error=77;Bits result;result.value=strtod(cases[i].text,&end);check(result.bits==cases[i].strtod_bits,24410+i);check(end==cases[i].text+cases[i].end,24440+i);check(*error==cases[i].error,24470+i);
  *error=77;result.value=atof(cases[i].text);check(result.bits==cases[i].atof_bits,24500+i);check(*error==77,24530+i);
 }
 U module=GetModuleHandleA("MSVCRT.DLL");typedef double(__cdecl*Strtod)(const char*,char**);typedef double(__cdecl*Atof)(const char*);
 Strtod ordinal_strtod=(Strtod)GetProcAddress(module,(const char*)711);Atof ordinal_atof=(Atof)GetProcAddress(module,(const char*)573);check(ordinal_strtod&&ordinal_atof,24560);
 Bits result;char*end=0;result.value=ordinal_strtod("2.5x",&end);check(result.bits==0x4004000000000000ull&&*end=='x',24561);result.value=ordinal_atof("-2.5");check(result.bits==0xc004000000000000ull,24562);
 result.value=strtod(".125",0);check(result.bits==0x3fc0000000000000ull,24563);
 // Reuse the real locale object; do not publish a separate decimal-point state.
 check(setlocale(0,"English_United States.1252")!=0,24564);result.value=strtod(" 1.25z",&end);check(result.bits==0x3ff4000000000000ull&&*end=='z',24565);check(setlocale(0,"C")!=0,24566);
 thread_floating_state();
}
#ifdef CRT_STRTOD_STANDALONE
int _fltused=0;
void mainCRTStartup(void){run_crt_strtod_test();ExitProcess(0);}
#endif
