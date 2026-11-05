// SPDX-License-Identifier: GPL-2.0-or-later
// x86 guest for the ordinal-only KERNEL32 exports 1..60. Every import below
// resolves by ordinal (NONAME in the .def); the names are only labels for
// the Win9x ordinals. Built with
// -DSTOP_ORDINAL=<40|45|51> the guest enters one of the 16-bit stack
// transitions instead, which must stop the run.
#include "kernel_ordinals_a_imports.h"
#define P(x) ((U)(x))
#define CHECK(x) do{if(!(x))ExitProcess(__LINE__);}while(0)
void *memset(void *p,int v,unsigned n){unsigned char*q=p;while(n--)*q++=v;return p;}
void *memcpy(void *p,const void *s,unsigned n){unsigned char*q=p;const unsigned char*r=s;while(n--)*q++=*r++;return p;}
static U equal(const void*a,const void*b,U n){const unsigned char*x=a,*y=b;while(n--)if(*x++!=*y++)return 0;return 1;}
static U length(const char*s){U n=0;while(s[n])++n;return n;}
typedef void (*Entry)(void);

/* VxDCall: push the service arguments, then the service, call, and report
   how many bytes the callee removed. The stack is restored afterwards. */
static U vxd_delta,vxd_before,vxd_after;
#define VXD_TAIL "call *%[fn]\n\tmov %%esp,%[a]\n\tmov %[b],%%esp\n\t"
static U vxd(Entry fn,U service,U count,const U *args){U result=0;
 switch(count){
 case 1:__asm__ volatile("mov %%esp,%[b]\n\tpush 0(%[p])\n\tpush %[svc]\n\t" VXD_TAIL
   :"=a"(result),[b]"=m"(vxd_before),[a]"=m"(vxd_after):[p]"r"(args),[svc]"r"(service),[fn]"r"(fn):"ecx","edx","memory","cc");break;
 case 2:__asm__ volatile("mov %%esp,%[b]\n\tpush 4(%[p])\n\tpush 0(%[p])\n\tpush %[svc]\n\t" VXD_TAIL
   :"=a"(result),[b]"=m"(vxd_before),[a]"=m"(vxd_after):[p]"r"(args),[svc]"r"(service),[fn]"r"(fn):"ecx","edx","memory","cc");break;
 case 3:__asm__ volatile("mov %%esp,%[b]\n\tpush 8(%[p])\n\tpush 4(%[p])\n\tpush 0(%[p])\n\tpush %[svc]\n\t" VXD_TAIL
   :"=a"(result),[b]"=m"(vxd_before),[a]"=m"(vxd_after):[p]"r"(args),[svc]"r"(service),[fn]"r"(fn):"ecx","edx","memory","cc");break;
 case 4:__asm__ volatile("mov %%esp,%[b]\n\tpush 12(%[p])\n\tpush 8(%[p])\n\tpush 4(%[p])\n\tpush 0(%[p])\n\tpush %[svc]\n\t" VXD_TAIL
   :"=a"(result),[b]"=m"(vxd_before),[a]"=m"(vxd_after):[p]"r"(args),[svc]"r"(service),[fn]"r"(fn):"ecx","edx","memory","cc");break;
 default:__asm__ volatile("mov %%esp,%[b]\n\tpush 16(%[p])\n\tpush 12(%[p])\n\tpush 8(%[p])\n\tpush 4(%[p])\n\tpush 0(%[p])\n\tpush %[svc]\n\t" VXD_TAIL
   :"=a"(result),[b]"=m"(vxd_before),[a]"=m"(vxd_after):[p]"r"(args),[svc]"r"(service),[fn]"r"(fn):"ecx","edx","memory","cc");break;}
 vxd_delta=vxd_before-vxd_after;return result;}
static U vxd1(Entry fn,U s,U a0){U v[]={a0};return vxd(fn,s,1,v);}
static U vxd2(Entry fn,U s,U a0,U a1){U v[]={a0,a1};return vxd(fn,s,2,v);}
static U vxd3(Entry fn,U s,U a0,U a1,U a2){U v[]={a0,a1,a2};return vxd(fn,s,3,v);}
static U vxd4(Entry fn,U s,U a0,U a1,U a2,U a3){U v[]={a0,a1,a2,a3};return vxd(fn,s,4,v);}
static U vxd5(Entry fn,U s,U a0,U a1,U a2,U a3,U a4){U v[]={a0,a1,a2,a3,a4};return vxd(fn,s,5,v);}
/* Int21Dispatch: EAX and ECX travel on the stack, EBX/EDX stay in registers,
   CF reports failure. */
static U int21_ebx,int21_ecx,int21_edx,int21_flags;
static U int21(Entry fn,U eax,U ebx,U ecx,U edx){U result;
 __asm__ volatile("mov %%esp,%[b]\n\tpush %%ecx\n\tpush %%eax\n\tpush $0x2a0010\n\tcall *%[fn]\n\tmov %%esp,%[a]\n\tmov %[b],%%esp\n\tpushfd\n\tpop %[fl]\n\tmov %%ebx,%[ob]\n\tmov %%ecx,%[oc]\n\tmov %%edx,%[od]\n\t"
  :"=a"(result),[b]"=m"(vxd_before),[a]"=m"(vxd_after),[fl]"=m"(int21_flags),[ob]"=m"(int21_ebx),[oc]"=m"(int21_ecx),[od]"=m"(int21_edx)
  :"a"(eax),"b"(ebx),"c"(ecx),"d"(edx),[fn]"r"(fn):"memory","cc");
 vxd_delta=vxd_before-vxd_after;return result;}
/* CommonUnimpStub: ECX carries the encoding; two argument dwords are pushed
   and the stub's own cleanup is measured before the stack is restored. */
static U unimp_pop;
static U unimp(U encoding){U result;
 __asm__ volatile("mov %%esp,%[b]\n\tpush $0x11111111\n\tpush $0x22222222\n\tmov %[enc],%%ecx\n\tcall *%[fn]\n\tmov %%esp,%[a]\n\tmov %[b],%%esp\n\t"
  :"=a"(result),[b]"=m"(vxd_before),[a]"=m"(vxd_after):[enc]"r"(encoding),[fn]"r"((Entry)CommonUnimpStub):"ecx","edx","memory","cc");
 unimp_pop=vxd_after-vxd_before+8;return result;}
static U mapped_alloc(U bytes){U edi=bytes;
 __asm__ volatile("call *%[fn]":"+D"(edi):[fn]"r"((Entry)AllocMappedBuffer):"eax","edx","memory","cc");return edi;}
static U mapped_free(U buffer){U edi=buffer;
 __asm__ volatile("call *%[fn]":"+D"(edi):[fn]"r"((Entry)FreeMappedBuffer):"eax","ecx","edx","memory","cc");return edi;}

static void oem_test(void){
 char out[64];unsigned char text[8];
 CHECK(k32CharToOemA(P("A\xc4\xe9z"),P(out))&&equal(out,"A\x8e\x82z",5));
 CHECK(k32OemToCharA(P("A\x8e\x82z\x0f"),P(out))&&equal(out,"A\xc4\xe9z\xa4",6));
 memset(out,'#',sizeof out);
 CHECK(k32CharToOemBuffA(P("\xc4\0\xe9"),P(out),3)&&equal(out,"\x8e\0\x82#",4));
 memset(out,'#',sizeof out);
 CHECK(k32OemToCharBuffA(P("\x8e\0\x14"),P(out),3)&&equal(out,"\xc4\0\xb6#",4));
 CHECK(k32CharToOemBuffA(P("x"),P(out),0));
 for(U c=0x80;c<0x100;++c){text[0]=(unsigned char)c;text[1]=0;CHECK(k32OemToCharA(P(text),P(out))&&out[0]&&!out[1]);}
 /* OEM code page 437 of the ISO installation: its best fit turns U+2018 into
    a grave accent and U+2019 into an apostrophe (code page 850 of the reference system
    installation turned both into 0x27). The line number is the exit code. */
 CHECK(k32CharToOemA(P("\x80\x91\x92"),P(out))&&out[0]&&out[1]==0x60&&out[2]==0x27);
 SetLastError(0);CHECK(!k32CharToOemA(0,P(out))&&!GetLastError());
 CHECK(!k32CharToOemA(P("a"),0)&&!GetLastError());
 CHECK(!k32OemToCharA(0,P(out))&&!k32CharToOemBuffA(P("a"),0,1)&&!k32OemToCharBuffA(0,P(out),1)&&!GetLastError());
 CHECK(!k32CharToOemA(P(0x10),P(out))&&GetLastError()==87);SetLastError(0);
 CHECK(!k32OemToCharA(P("a"),P(0x10))&&GetLastError()==87);SetLastError(0);
 CHECK(!k32CharToOemBuffA(P("a"),P(0x10),1)&&GetLastError()==87);SetLastError(0);
 CHECK(!k32OemToCharBuffA(P(0x10),P(out),1)&&GetLastError()==87);SetLastError(0);
}
static void string_test(void){
 char out[64];U args[3]={7,P("q"),0x2a};
 CHECK(k32wsprintfA(P(out),P("%d-%s-%x"),7,P("q"),0x2a)==6&&equal(out,"7-q-2a",7));
 CHECK(k32wvsprintfA(P(out),P("%d-%s-%x"),P(args))==6&&equal(out,"7-q-2a",7));
 CHECK(!k32LoadStringA(0,1,P(out),0)&&!k32LoadStringA(0,1,0,8));
 CHECK(!k32LoadStringA(0,7,P(out),sizeof out)); /* no string table in this image */
}
/* The wide conversions of the formatter, over the ANSI code page of the
   session. %C and %S are wide unless an h stands before them, a character of
   no range becomes an underscore, and the precision counts characters. */
static void wide_test(void){
 char out[64];U args[2];
 static const unsigned short text[]={'W','i','n','9','8',0};
 static const unsigned short euro[]={0x20ac,0};
 static const unsigned short mixed[]={'A',0x0100,'B',0};
 CHECK(k32wsprintfA(P(out),P("[%ls]"),P(text))==7&&equal(out,"[Win98]",8));
 CHECK(k32wsprintfA(P(out),P("[%S]"),P(text))==7&&equal(out,"[Win98]",8));
 CHECK(k32wsprintfA(P(out),P("[%ws]"),P(text))==7&&equal(out,"[Win98]",8));
 CHECK(k32wsprintfA(P(out),P("[%8.3ls]"),P(text))==10&&equal(out,"[     Win]",11));
 CHECK(k32wsprintfA(P(out),P("[%-8ls]"),P(text))==10&&equal(out,"[Win98   ]",11));
 CHECK(k32wsprintfA(P(out),P("[%.0ls]"),P(text))==2&&equal(out,"[]",3));
 CHECK(k32wsprintfA(P(out),P("[%ls]"),P(mixed))==5&&equal(out,"[A_B]",6));
 CHECK(k32wsprintfA(P(out),P("[%ls]"),P(euro))==3&&equal(out,"[\x80]",4));
 CHECK(k32wsprintfA(P(out),P("[%lc]"),0xe9)==3&&equal(out,"[\xe9]",4));
 CHECK(k32wsprintfA(P(out),P("[%C]"),0x20ac)==3&&equal(out,"[\x80]",4));
 CHECK(k32wsprintfA(P(out),P("[%C]"),0x4e00)==3&&equal(out,"[_]",4));
 CHECK(k32wsprintfA(P(out),P("[%5lc]"),'A')==7&&equal(out,"[    A]",8));
 CHECK(k32wsprintfA(P(out),P("[%hC]"),'A')==3&&equal(out,"[A]",4));
 CHECK(k32wsprintfA(P(out),P("[%lc]"),0)==2&&equal(out,"[]",3));
 /* A null argument reads address 0, which Win9x has mapped and NEO98 does
    not; tools/test_printf.py compares that case against the original. */
 args[0]=P(text);args[1]=0x41;
 CHECK(k32wvsprintfA(P(out),P("[%ls|%lc]"),P(args))==9&&equal(out,"[Win98|A]",10));
}
static void unimp_test(void){
 SetLastError(0);CHECK(unimp(0xf2)==~0u&&unimp_pop==8&&GetLastError()==120);
 SetLastError(0);CHECK(unimp(0xe0)==120&&unimp_pop==0&&GetLastError()==120);
 SetLastError(0);CHECK(unimp(0xd1)==50&&unimp_pop==4&&GetLastError()==120);
 SetLastError(0);CHECK(unimp(0x13)==1&&unimp_pop==12&&GetLastError()==120);
 SetLastError(0);CHECK(unimp(0x04)==0&&unimp_pop==16&&GetLastError()==120);
 SetLastError(0);CHECK(unimp(0x00)==0&&unimp_pop==0&&GetLastError()==120);
}
static void thunk_test(void){
 U count=0x12345678,block[16];
 SetLastError(0);CHECK(!ThunkTheTemplateHandle(0x1234)&&GetLastError()==6);SetLastError(0);
 CHECK(GDIReallyCares(GetModuleHandleA(0)));CHECK(!GDIReallyCares(0x77000000));
 LogApiThkLSF(P("x"));LogApiThkSL(P("y"));LogCBThkSL(P("z"));
 HouseCleanLogicallyDeadHandles();
 U env=GetWin16DOSEnv();CHECK(env&&env==GetWin16DOSEnv());
 U found=0;for(const char*e=(const char*)env;*e;e+=length(e)+1)if(e[0]&&length(e)>1)found=1;CHECK(found);
 CHECK(ThunkInitLS(0,P("Thk"),0x1234,P("SYSTHUNK.DLL"),0)==1);
 CHECK(ThunkInitLS(P(block),P("Thk"),0x1234,P("SYSTHUNK.DLL"),0)&1);
 CHECK(!ThunkInitLS(P(block),P("Thk"),0x1234,P("SYSTHUNK.DLL"),P("LZ32.DLL")));
 CHECK(!ThunkInitSL(P(block),P("Thk"),0x1234,P("SYSTHUNK.DLL"),P("SYSTHUNK.DLL")));
 CHECK(RestoreThunkLock(0xffff));
 CHECK(ReleaseThunkLock(P(&count))==P(&count)&&count==0xffffffffu);
 CHECK(RestoreThunkLock(5)&&ReleaseThunkLock(P(&count))&&count==5);
 CHECK(RestoreThunkLock(0xffff)&&ReleaseThunkLock(P(&count))&&count==0xffffffffu);
 SetLastError(0);CHECK(!ReleaseThunkLock(P(0x10))&&GetLastError()==87);SetLastError(0);
 CHECK(!GetThunkBuff()&&!GetThunkStuff(P("SYSTHUNK.DLL"),P("ThkBuf")));
 U buffer=mapped_alloc(24);CHECK(buffer&&((U*)buffer)[-2]&&K32WOWGetVDMPointer(((U*)buffer)[-1],24,1)==buffer);
 memset((void*)buffer,0x5a,24);
 U header=mapped_free(buffer);CHECK(header==buffer-8);
 CHECK(mapped_free(0)==0);
}
static void heap16_test(void){
 U h=GlobalAlloc16(0x42,100);CHECK(h&&(h&7)==6);
 U p=GlobalLock16(h);CHECK(p&&GlobalSize16(h)==100);memset((void*)p,0,100);((char*)p)[99]='x';
 CHECK(GlobalLock16(h)==p);CHECK(GlobalUnlock16(h)==1&&GlobalUnlock16(h)==0);
 CHECK(GlobalWire16(h)==p&&GlobalUnWire16(h));GlobalFix16(h);GlobalUnfix16(h);
 U f=GlobalAlloc16(0,16);CHECK(f&&(f&7)==7&&GlobalLock16(f)&&GlobalUnlock16(f)==0);
 CHECK(!GlobalFree16(h)&&!GlobalFree16(f));CHECK(GlobalFree16(h)==h);
 CHECK(!GlobalLock16(h)&&!GlobalSize16(h)&&!GlobalLock16(0x1234));
 U w=K32WOWGlobalAlloc16(0x40,32);CHECK(w&&K32WOWGlobalLock16(w)&&K32WOWGetVDMPointer(K32WOWGlobalLock16(w),32,1)==GlobalLock16(w));
 CHECK(!GlobalFree16(w)&&!K32WOWGlobalLock16(w));
}
static void wow_test(void){
 U code=0x5555;
 CHECK(!K32WOWCallback16(0x01230000,7));
 SetLastError(0);CHECK(!K32WOWCallback16Ex(0x01230000,0,20,P(&code),P(&code))&&GetLastError()==87&&code==0x5555);SetLastError(0);
 CHECK(!K32WOWCallback16Ex(0x01230000,0,4,P(&code),P(&code))&&!GetLastError()&&code==0x5555);
 CHECK(K32WOWGetVDMPointer(0x12345678,4,0)==0x179b8);
 CHECK(!K32WOWGetVDMPointer(0x00080010,4,1)&&!K32WOWGetVDMPointer(0x01070010,4,1)&&!K32WOWGetVDMPointer(0x00070010,4,1));
 CHECK(K32WOWHandle32(0x1234,0)==0x1234&&K32WOWHandle16(0x12345678,4)==0x5678);
 SetLastError(0);CHECK(!K32WOWHandle32(0x1234,3)&&GetLastError()==50);SetLastError(0);
 CHECK(!K32WOWHandle16(0x1234,3)&&GetLastError()==50);SetLastError(0);
 CHECK(!K32WOWHandle32(0x1234,13)&&!GetLastError());
 CHECK(!K32WOWHandle16(GetCurrentProcessId(),13)&&!GetLastError());
 CHECK(!K32WOWHandle16(0x7abc1234,13)&&GetLastError()==87);SetLastError(0);
}
static void vxd_test(void){
 U counter[2]={0,0};
 CHECK(vxd1((Entry)VxDCall0,0x2a0009,1)==0&&vxd_delta==0);
 CHECK(vxd1((Entry)VxDCall1,0x2a0044,P(counter))&&vxd_delta==0&&(counter[0]|counter[1]));
 U base=vxd3((Entry)VxDCall2,0x10000,0x80000400,4,0);CHECK(base!=~0u&&base&&!(base&0xfff)&&vxd_delta==0);
 CHECK(vxd5((Entry)VxDCall3,0x10001,base>>12,4,0,0,0x60000)==1&&vxd_delta==0);
 *(U*)base=0x1234;CHECK(*(U*)(base+0x3ffc)==0);
 U permissions=vxd4((Entry)VxDCall4,0x1000d,base>>12,1,0,0x40000);CHECK(permissions==0x60000&&vxd_delta==0);
 CHECK(vxd3((Entry)VxDCall5,0x10002,base>>12,4,0)==1&&vxd_delta==0);
 CHECK(vxd2((Entry)VxDCall6,0x1000a,base,0)==1&&vxd_delta==0);
 CHECK(vxd5((Entry)VxDCall7,0x10001,base>>12,4,0,0,0x60000)==0&&vxd_delta==0); /* released region */
 CHECK(vxd2((Entry)VxDCall8,0x1000a,base,0)==0&&vxd_delta==0);
 /* VMM carries the registry too. Service 1Ch is the body behind ADVAPI32's
    RegFlushKey : one argument, the key, and the Win32 status in
    EAX. scanregw.exe out of the autostart calls it
    directly, without ADVAPI32. */
 CHECK(vxd1((Entry)VxDCall1,0x1001c,0x80000002)==0&&vxd_delta==0);
 CHECK(vxd1((Entry)VxDCall0,0x1001c,0x484a574d)==6&&vxd_delta==0);
 U drive=int21((Entry)VxDCall8,0x1900,0,0,0);CHECK((drive&0xff)==2&&!(int21_flags&1)&&vxd_delta==0);
 U space=int21((Entry)VxDCall8,0x3600,0,0,3);CHECK((space&0xffff)!=0xffff&&(space&0xffff)&&(int21_ecx&0xffff)&&(int21_edx&0xffff)&&!(int21_flags&1)&&vxd_delta==0);
 U bad=int21((Entry)VxDCall8,0x3600,0,0,26);CHECK((bad&0xffff)==0xffff&&vxd_delta==0);
}
void mainCRTStartup(void){
#ifdef STOP_ORDINAL
#if STOP_ORDINAL==40
 OT_32ThkLSF();
#elif STOP_ORDINAL==45
 Common32ThkLS();
#else
 W32S_BackTo32();
#endif
 ExitProcess(99);
#else
 oem_test();string_test();wide_test();unimp_test();thunk_test();heap16_test();wow_test();vxd_test();
 ExitProcess(0);
#endif
}
