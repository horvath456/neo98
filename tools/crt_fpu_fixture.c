// SPDX-License-Identifier: GPL-2.0-or-later
// Real x87 call ABI: _ftol consumes ST(0), returns EDX:EAX and restores CW.
typedef unsigned U;typedef unsigned long long Q;
#define API __declspec(dllimport) __stdcall
#define CRT __declspec(dllimport) __cdecl
API void ExitProcess(U);API U GetModuleHandleA(const char*);API U GetProcAddress(U,const char*);
CRT void _ftol(void);CRT void _fpreset(void);CRT U _statusfp(void);CRT U _clearfp(void);
typedef void(__cdecl*Ftol)(void);
struct __attribute__((packed)) Extended{Q significand;unsigned short exponent;};
static void check(U okay,U error){if(!okay)ExitProcess(error);}
static Q convert(struct Extended value,Ftol function){
 U low,high;
 __asm__ volatile("fldt %2\n\tcall *%3":"=a"(low),"=d"(high):"m"(value),"r"(function):"ecx","memory","cc");
 return ((Q)high<<32)|low;
}
static const struct{Q significand;unsigned short exponent;Q integer;U flags;}cases[]={
 {0,0,0,0},{0,0x8000,0,0},
 {0xa000000000000000ull,0x3fff,1,1},
 {0xa000000000000000ull,0xbfff,0xffffffffffffffffull,1},
 {0xffffffffffffffffull,0x3ffe,0,1},
 {1,0,0,1},
 {0xffffffffffffffffull,0x403d,0x7fffffffffffffffull,1},
 {0x8000000000000000ull,0xc03e,0x8000000000000000ull,0},
 {0x8000000000000000ull,0x403e,0x8000000000000000ull,0x10},
 {0x8000000000000000ull,0x7fff,0x8000000000000000ull,0x10},
 {0xc000000000000000ull,0x7fff,0x8000000000000000ull,0x10},
};
void run_crt_fpu_test(void){
 _fpreset();U env[7];__asm__ volatile("fnstenv %0\n\tfldenv %0":"=m"(env));
 // Original CRT reset uses53-bit precision. Unicorn preserves the encoded023F.
 check((env[0]&65535)==0x23f&&(env[1]&65535)==0&&(env[2]&65535)==0xffff,26000);
 U module=GetModuleHandleA("MSVCRT.DLL");Ftol ordinal=(Ftol)GetProcAddress(module,(const char*)242);check(ordinal!=0,26001);
 for(U i=0;i<sizeof cases/sizeof cases[0];++i){
  _clearfp();unsigned short control=0xb7f,actual=0;
  __asm__ volatile("fldcw %0\n\tfld1"::"m"(control));
  struct Extended x={cases[i].significand,cases[i].exponent};Q result=convert(x,i&1?ordinal:_ftol);
  check(result==cases[i].integer,26010+i);
  __asm__ volatile("fnstcw %0":"=m"(actual));check(actual==control,26030+i);
  check(_statusfp()==cases[i].flags,26050+i);
  check(_clearfp()==cases[i].flags&&_statusfp()==0,26070+i);
  Q held=0;__asm__ volatile("fstpl %0":"=m"(held));check(held==0x3ff0000000000000ull,26090+i);
 }
 // All six x87 exception bits share the original CRT bit mapping.
 __asm__ volatile("fnstenv %0":"=m"(env));env[0]=(env[0]&0xffff0000u)|0x37f;env[1]=(env[1]&0xffff0000u)|0x3f;
 __asm__ volatile("fldenv %0"::"m"(env));check(_statusfp()==0x8001f,26110);check(_clearfp()==0x8001f&&_statusfp()==0,26111);
 // _fpreset is x87-only; unlike fresh-thread initialization it preserves MXCSR.
 U mxcsr=0x3f80,actual_mxcsr=0;__asm__ volatile("ldmxcsr %0\n\tfld1"::"m"(mxcsr));_fpreset();
 __asm__ volatile("stmxcsr %0\n\tfnstenv %1\n\tfldenv %1":"=m"(actual_mxcsr),"=m"(env));
 check(actual_mxcsr==mxcsr&&(env[2]&65535)==0xffff&&(env[0]&65535)==0x23f,26112);
 // Masked stack underflow returns the integer-indefinite and sets IE/SF.
 U low,high;Ftol function=_ftol;__asm__ volatile("call *%2":"=a"(low),"=d"(high):"r"(function):"ecx","memory","cc");
 check(low==0&&high==0x80000000&&_statusfp()==0x10,26113);_fpreset();
}
#ifdef CRT_FPU_STANDALONE
int _fltused=0;
void mainCRTStartup(void){run_crt_fpu_test();ExitProcess(0);}
#endif
