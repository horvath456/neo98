// SPDX-License-Identifier: GPL-2.0-or-later
// x86 guest fixture for the KERNEL32 ordinal-only exports 61..122. Every entry
// is imported by ordinal (NONAME in the.def); ord<N> is the local name of
// ordinal N. The command line selects a mode: the default runs the entries
// that succeed, "mode=<N>" drives one entry that the native port reports as
// unsupported, and the runner expects that run to stop with a documented error.
#include "kernel_ordinals_b_imports.h"
#define P(x) ((U)(x))
#define CHECK(x) do{if(!(x))ExitProcess(__LINE__);}while(0)
typedef unsigned short W;
static U worker_seen, level_worker_done;
static U length(const char*s){U n=0;while(s[n])++n;return n;}
static U equalw(const W*a,const W*b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
static U mode(void){
 const char*c=(const char*)GetCommandLineA();
 for(;*c;++c)if(c[0]=='m'&&c[1]=='o'&&c[2]=='d'&&c[3]=='e'&&c[4]=='='){U v=0;for(c+=5;*c>='0'&&*c<='9';++c)v=v*10+(U)(*c-'0');return v;}
 return 0;
}
static U __stdcall sum3(U a,U b,U c){return a+b+c;}
static U __stdcall seventy_seven(void){return 77;}
static U __stdcall waiter(U event){WaitForSingleObject(event,~0u);return 9;}
static U __stdcall level_worker(U lock){worker_seen=1;ord97(lock);level_worker_done=1;ord98(lock);return 3;}
static void large_integers(void){
 CHECK(ord72(0xffffffffu,0,1,0)==0x100000000ull);
 CHECK(ord72(0x80000000u,0x7fffffffu,0x80000000u,0)==0x8000000000000000ull);
 CHECK(ord83(0,1,1,0)==0xffffffffull);
 CHECK(ord73(0xffffffffu,7)==0xfffffffffffffff9ull);   /* -1 * 7 */
 CHECK(ord74(0xffffffffu,7)==0x6fffffff9ull);
 CHECK(ord82(1,0)==0xffffffffffffffffull&&ord82(0,0x80000000u)==0x8000000000000000ull);
 CHECK(ord84(0x80000000u)==0xffffffff80000000ull&&ord85(0x80000000u)==0x80000000ull);
 CHECK(ord79(1,0,33)==0x200000000ull&&ord79(0x80000000u,0,1)==0x100000000ull&&ord79(5,0,64)==5);
 CHECK(ord80(0,1,32)==1&&ord80(0,0x80000000u,1)==0x4000000000000000ull);
 CHECK(ord81(0,0x80000000u,32)==0xffffffff80000000ull&&ord81(0,0x80000000u,1)==0xc000000000000000ull);
 U remainder=0;
 CHECK(ord75(0x11,0,5,P(&remainder))==3&&remainder==2);
 CHECK(ord75(0x00000001u,0x00000001u,0x00000002u,0)==0x80000000u);
 remainder=99;CHECK(ord76(0x11,0,5,P(&remainder))==3&&remainder==2);
 CHECK(ord76(0,1,3,P(&remainder))==0x55555555ull&&remainder==1);
 remainder=99;CHECK(ord76(7,9,0,P(&remainder))==0x900000007ull&&remainder==99);
 CHECK(ord78(0xfffffffeu,0xffffffffu,3)==0xfffffffffffffffaull);   /* -2 * 3 */
 CHECK(ord78(0x80000000u,0,0xffffffffu)==0xffffffff80000000ull);   /* 2^31 * -1 */
 /* 100 / 7 through the magic constant 0x92492492 49249249, shift 2. */
 CHECK(ord77(100,0,0x49249249u,0x92492492u,2)==14);
 CHECK(ord77((U)-100,0xffffffffu,0x49249249u,0x92492492u,2)==0xfffffffffffffff2ull);
}
static void strings(void){
 static const W hex[]={' ','-','0','x','1','F',' ','r',0};const W*end=0;
 CHECK(ord105(P(hex),P(&end),0)==(U)-31&&end==hex+6);
 static const W big[]={'9','9','9','9','9','9','9','9','9','9','9',0};
 CHECK(ord105(P(big),0,10)==0x7fffffffu);
 static const W neg[]={'-','9','9','9','9','9','9','9','9','9','9','9','z',0};
 CHECK(ord105(P(neg),P(&end),10)==0x80000000u&&end==neg+12);
 static const W zz[]={'z','z',0};CHECK(ord105(P(zz),0,36)==1295);
 CHECK(ord105(P(zz),P(&end),1)==0&&end==zz);
 static const W oct[]={'0','1','7',0};CHECK(ord105(P(oct),0,0)==15);
 static const W none[]={' ','+',0};CHECK(ord105(P(none),P(&end),10)==0&&end==none);
 const char*text="hello world";
 CHECK(ord108(P(text),'o')==P(text+4)&&ord108(P(text),0)==P(text+11)&&ord108(P(text),'z')==0);
 CHECK(ord111(P(text),'o')==P(text+7)&&ord111(P(text),'h')==P(text)&&ord111(P(text),'z')==0);
 CHECK(ord109(P(text),P("wo"))==4&&ord109(P(text),P("xyz"))==11&&ord109(P(text),P("h"))==0);
 CHECK(ord112(P(text),P("o w"))==P(text+4)&&ord112(P(text),P(""))==P(text)&&ord112(P(text),P("worlds"))==0);
 char narrow[8];for(U i=0;i<8;++i)narrow[i]='#';
 CHECK(ord110(P(narrow),P("ab"),6)==P(narrow)&&narrow[0]=='a'&&narrow[1]=='b'&&!narrow[2]&&!narrow[5]&&narrow[6]=='#');
 CHECK(ord110(P(narrow),P("wxyz"),2)==P(narrow)&&narrow[0]=='w'&&narrow[1]=='x'&&!narrow[2]);
 static const W wide[]={'a','b','c',0},wide2[]={'a','b','d',0},wide3[]={'a','b',0};
 CHECK(ord113(P(wide),'c')==P(wide+2)&&ord113(P(wide),0)==P(wide+3)&&ord113(P(wide),'q')==0);
 CHECK(ord114(P(wide),P(wide))==0&&ord114(P(wide),P(wide2))==(U)-1&&ord114(P(wide2),P(wide))==1&&ord114(P(wide),P(wide3))==1);
 CHECK(ord115(P(wide),P(wide2),2)==0&&ord115(P(wide),P(wide2),3)==(U)-1&&ord115(P(wide),P(wide2),0)==0);
 W copy[6];for(U i=0;i<6;++i)copy[i]='#';
 CHECK(ord116(P(copy),P(wide3),5)==P(copy)&&copy[0]=='a'&&copy[1]=='b'&&!copy[2]&&!copy[4]&&copy[5]=='#');
 static const W lower[]={'a','b','c',0},upper[]={'A','B','C',0};
 CHECK(ord106(P(lower),P(upper))==0&&(int)ord106(P(wide2),P(lower))>0&&(int)ord106(P(lower),P(wide2))<0);
 W mixed[]={'h','e','l','l','o','1',0};static const W shout[]={'H','E','L','L','O','1',0};
 CHECK(ord107(P(mixed))==P(mixed)&&equalw(mixed,shout));
 CHECK(ord117('A',1)&&!ord117('a',1)&&ord117('a',2)&&ord117('7',4)&&!ord117('7',1));
 CHECK(ord119('a')=='A'&&ord119('Z')=='Z'&&ord119('1')=='1');
}
static void wow(void){
 CHECK(ord68(0x12340010u,~0u,0)==0x12350&&ord68(0xffffffffu,4,0)==0x10ffef);
 CHECK(!ord68(0x000f0010u,4,1)&&!ord68(0x00080010u,4,1)&&!ord68(0x00000010u,4,1));
 CHECK(!ord69(0x000f0010u));
 U descriptor[2]={7,7};
 CHECK(!ord70(0x000f0000u,P(descriptor))&&!ord70(0,P(descriptor))&&!ord70(0x00080000u,P(descriptor))&&descriptor[0]==7);
 /* Ordinals 59 and 60 (alloc and lock) belong to the other batch; 63 allocates
    and locks in one step, 65 locks again and reports the size. */
 U h=0;U p=ord63(0x42,64,P(&h));CHECK(p&&h&&h<0x10000);U flat=ord68(p,64,1);CHECK(flat&&flat!=p);*(U*)flat=0x11223344u;ord69(p);
 CHECK(ord70(p,P(descriptor))&&((descriptor[0]>>16)|((descriptor[1]&255)<<16)|(descriptor[1]&0xff000000u))==flat);
 U size=0;CHECK(ord65(h,P(&size))==p&&size>=64&&*(U*)flat==0x11223344u);
 CHECK(ord61(h)==1&&ord61(h)==0&&ord61(h)==0);
 CHECK(ord62(h)==0&&ord62(h)==h&&!ord65(h,P(&size)));
 U h2=0;U p2=ord63(0x40,32,P(&h2));CHECK(p2&&h2);
 CHECK(ord64(p2)==0&&ord64(p2)==0&&!ord65(h2,P(&size))&&!ord61(h2)&&ord62(h2)==h2);
 CHECK(!ord63(0,0x7ffffff0u,P(&h2)));
 U h3=0;U p3=ord63(0,8,0);CHECK(p3&&ord64(p3)==0);
 ord66();ord67(0);
}
static void levels(void){
 U*lock=0;ord93(P(&lock));CHECK(lock&&lock[0]==4&&lock[4]==1);
 CHECK(ord96()==0);ord97(P(lock));CHECK(ord96()==1);ord97(P(lock));CHECK(ord96()==2);
 ord98(P(lock));CHECK(ord96()==1);ord86(P(lock));CHECK(ord96()==0);
 ord94(P(lock));ord95(P(lock));
 U level[8]={4,0,0,0,1,0,0,0};ord97(P(level));ord97(P(level));ord98(P(level));ord98(P(level));
 ord97(P(lock));U tid=0;U thread=CreateThread(0,0,P(level_worker),P(lock),0,P(&tid));CHECK(thread);
 for(U i=0;i<8;++i)Sleep(0);
 CHECK(worker_seen&&!level_worker_done&&ord96()==1);
 ord98(P(lock));CHECK(WaitForSingleObject(thread,5000)==0&&level_worker_done);
 U code=0;CHECK(GetExitCodeThread(thread,P(&code))&&code==3&&CloseHandle(thread));
 CHECK(!ord71(tid));
}
static void threads(void){
 CHECK(ord71(GetCurrentThreadId())==1&&!ord71(0x7fff)&&!ord71(0));
 CHECK(ord87()==0);
 CHECK(ord88(12,0,P(sum3),1,2,3)==6&&ord88(0,1,P(seventy_seven))==77);
 U event=CreateEventA(0,1,0,0);CHECK(event);
 U tid=0;U thread=CreateThread(0,0,P(waiter),event,0,P(&tid));CHECK(thread);
 Sleep(0);CHECK(ord71(tid)==1);
 CHECK(ord100(thread,5,1)==1&&WaitForSingleObject(thread,5000)==0);
 U code=0;CHECK(GetExitCodeThread(thread,P(&code))&&code==5&&!ord71(tid));
 CHECK(!ord100(0x1234,0,0)&&CloseHandle(thread)&&CloseHandle(event));
 for(U i=0;i<8;++i)CHECK(ord102()==0x50+i);
 SetLastError(0);CHECK(ord102()==0x4f&&GetLastError()==259);
 CHECK(ord103(0x50)==1);SetLastError(0);CHECK(!ord103(0x50)&&GetLastError()==87);
 SetLastError(0);CHECK(!ord103(0x60)&&GetLastError()==87&&!ord103(0x4f));
 CHECK(ord102()==0x50&&ord103(0x57)==1&&ord102()==0x57);
 CHECK(ord92()==0&&ord120()==0xc0887b5bu);
 CHECK(ord122(0,0)==0&&ord122(0,0x12345678u)==0&&ord122(7,0)==7);
 U desktop=GetDesktopWindow();CHECK(desktop&&IsWindow(desktop)&&ord122(0,desktop)==1&&ord122(0,0)==0);
 char name[4]={'x','y',0,0};ord91(P(name));CHECK(!name[0]);
 U eax=0;__asm__ volatile("push $5\n\tmovl $0x1234,%%eax\n\tcall _ord101@4\n\tmovl %%eax,%0":"=r"(eax)::"eax","ecx","edx","memory");CHECK(eax==0x1234);
}
U prime_area;
U ft_prime(void);U qt_prime(void);U fragment(U record);
__asm__(".globl _ft_prime\n_ft_prime:\n movw $11,%cx\n pushl $1f\n pushl _prime_area\n pushl $-4\n jmp _ord89@8\n"
 "1: call *__imp__FT_Thunk@0\n movswl %ax,%ebx\n jmp *__imp__FT_Exit0@0\n"
 ".globl _qt_prime\n_qt_prime:\n pushl %ebp\n movl %esp,%ebp\n pushl $11\n subl $0x3c,%esp\n"
 " movl _prime_area,%eax\n movl $-4,%edx\n call _ord90@0\n leave\n ret\n"
 ".globl _fragment\n_fragment:\n pushl %ebp\n pushl %ebx\n movl 12(%esp),%ebx\n pushl $2f\n movl %esp,%ebp\n jmp _ord104@0\n"
 "2: popl %ebx\n popl %ebp\n ret\n");
static U __stdcall fragment_target(U a,U b,U c,U d){CHECK(a==4&&b==3&&c==2&&d==1);return 0xabcdef12;}
static void primes(U which){
 if(which==122){
  U file=CreateFileA(P("C:\\WINDOWS\\APPLOG\\APPLOG.ind"),0x80000000,0,0,3,0x80,0);CHECK(file!=~0u);
  CHECK(ord122(1,0)==1&&GetLastError()==32);CHECK(CloseHandle(file));
  CHECK(ord122(1,0)==1);CHECK(ord122(1,0)==1);return;
 }
 if(which==121){
  U block[16]={0x3130534c,0x2e50f};U code[9]={0};
  CHECK(ord121(1,1,1,1,1,2)==1); // Thread notification must not touch pointers.
  CHECK(!ord121(P(block),P("missing"),P("shell.dll"),P("TEST.EXE"),0,1));
  CHECK(!block[2]);block[1]^=1;
  CHECK(!ord121(P(block),P("Shl3216_ThunkData16"),P("shell.dll"),P("TEST.EXE"),0,1));
  CHECK(!block[2]);block[1]^=1;
  CHECK(ord121(P(block),P("Shl3216_ThunkData16"),P("shell.dll"),P("TEST.EXE"),0,1));
  CHECK(block[2]);code[0]=block[2];prime_area=P(code+1);CHECK(qt_prime()==1);
  CHECK(ord121(P(block),0,0,0,0,0)&&!block[2]);
  block[0]=0x1234;CHECK(ord121(P(block),0,0,0,0,0));return;
 }
 if(which==104){
  unsigned char record[48]={};U values[5]={1,2,3,4,P(fragment_target)};
  for(U i=0;i<5;++i)for(U j=0;j<4;++j)record[0x16+4*i+j]=(unsigned char)(values[i]>>(j*8));
  U lock=0;ord93(P(&lock));ord97(lock);
  CHECK(fragment(P(record))==GetCurrentThreadId()&&ord96()==1);ord98(lock);return;
 }
 static U block[16]={0x3130534c,0x2e50f};static U code[9];
 CHECK(ThunkConnect32(P(block),P("Shl3216_ThunkData16"),P("shell.dll"),P("KERNEL_ORDINALS_B.EXE"),GetModuleHandleA(0),1));
 code[0]=block[2];prime_area=P(code+1);
 CHECK((which==89?ft_prime():qt_prime())==1);
 CHECK((which==89?ft_prime():qt_prime())==1);
 CHECK(ThunkConnect32(P(block),0,0,0,0,0));
}
static void service_or_fault(U which){
 switch(which){
 case 101:{
  SetLastError(0x9876);U result=0;
  __asm__ volatile("push $0\n\tmovl $0x12345678,%%eax\n\tcall _ord101@4\n\tmovl %%eax,%0":"=r"(result)::"eax","ecx","edx","memory");
  CHECK(result==0x12345678&&GetLastError()==0x9876);Sleep(0);
  U handle=CreateFileA(P("C:\\WINDOWS\\TEMP\\CACHE101.BIN"),0xc0000000,0,0,2,0,0);CHECK(handle!=~0u);
  U bytes=0,source=0xabcdef01,destination=0;CHECK(WriteFile(handle,P(&source),4,P(&bytes),0)&&bytes==4);
  CHECK(SetFilePointer(handle,0,0,0)==0);CHECK(ReadFile(handle,P(&destination),4,P(&bytes),0)&&bytes==4&&destination==source);
  CHECK(CloseHandle(handle));ord101(0);ExitProcess(0);return;
 }


 case 75:ord75(1,0,0,0);break;
 default:ExitProcess(200);
 }
 ExitProcess(201);
}
void mainCRTStartup(void){
 const U which=mode();
 if(which==89||which==90||which==104||which==121||which==122){primes(which);ExitProcess(0);}
 if(which){service_or_fault(which);}
 large_integers();strings();wow();levels();threads();
 ExitProcess(0);
}
