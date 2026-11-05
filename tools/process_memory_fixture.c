// SPDX-License-Identifier: GPL-2.0-or-later
// The public x86 imports and generated-code path used by managed runtimes.
typedef unsigned U;
typedef unsigned short W;
#define API __declspec(dllimport) __stdcall
void API ExitProcess(U);
U API GetLastError(void);
void API SetLastError(U);
U API GetCurrentProcess(void);
U API GetCommandLineA(void);
U API GetCommandLineW(void);
void API GetSystemInfo(U*);
U API GetProcessAffinityMask(U,U*,U*);
U API IsDebuggerPresent(void);
U API ReadProcessMemory(U,const void*,void*,U,U*);
U API WriteProcessMemory(U,void*,const void*,U,U*);
U API VirtualAlloc(void*,U,U,U);
U API VirtualFree(void*,U,U);
U API VirtualQuery(const void*,void*,U);
U API VirtualQueryEx(U,const void*,void*,U);
U API VirtualProtect(void*,U,U,U*);
U API VirtualProtectEx(U,void*,U,U,U*);
U API VirtualUnlock(void*,U);
U API FlushInstructionCache(U,const void*,U);
#define CHECK(x) do {if(!(x))ExitProcess(__LINE__);}while(0)
static unsigned char source[9000],output[9000];
void mainCRTStartup(void){
 U info[9],sig,b,c,d;
 GetSystemInfo(info);
 __asm__("cpuid":"=a"(sig),"=b"(b),"=c"(c),"=d"(d):"a"(1),"c"(0));
 CHECK(!info[0]&&info[1]==4096&&info[2]==0x400000&&info[3]==0x7fffffff&&info[4]==1&&info[5]==1&&info[7]==65536);
 CHECK((info[8]&65535)==((sig>>8)&15));CHECK((info[8]>>16)==(((sig&0xf0)<<4)|(sig&15)));
 U process=GetCurrentProcess(),pm=0,sm=0;
 CHECK(GetProcessAffinityMask(process,&pm,&sm)&&pm==1&&sm==1);
 CHECK(!GetProcessAffinityMask(0,&pm,&sm)&&GetLastError()==6);
 CHECK(!IsDebuggerPresent());
 const W *wide=(const W*)GetCommandLineW();CHECK(wide&&(U)wide==GetCommandLineW());
 char *ansi=(char*)GetCommandLineA();U i;
 for(i=0;ansi[i];++i)CHECK(wide[i]==(unsigned char)ansi[i]);CHECK(!wide[i]);
 char before=ansi[0];ansi[0]='!';CHECK(wide[0]==(unsigned char)before);ansi[0]=before;
 unsigned char *memory=(unsigned char*)VirtualAlloc(0,12288,0x3000,4);CHECK(memory);
 for(i=0;i<9000;++i)source[i]=(unsigned char)(i*13);
 U done=~0u;CHECK(WriteProcessMemory(process,memory,source,9000,&done)&&done==9000);
 CHECK(ReadProcessMemory(process,memory,output,9000,&done)&&done==9000);
 for(i=0;i<9000;++i)CHECK(output[i]==source[i]);
 // Check overlap across the internal transfer-buffer boundary.
 CHECK(WriteProcessMemory(process,memory+7,memory,8000,&done)&&done==8000);
 for(i=0;i<8000;++i)CHECK(memory[i+7]==source[i]);
 U query[7],direct[7];CHECK(VirtualQueryEx(process,memory+73,query,28)==28&&VirtualQuery(memory+73,direct,28)==28);
 for(i=0;i<7;++i)CHECK(query[i]==direct[i]);CHECK(query[0]==(U)memory&&query[4]==0x1000&&query[5]==4);
 SetLastError(123);CHECK(!VirtualQueryEx(process,memory,query,27)&&GetLastError()==123);
 CHECK(!VirtualQueryEx(process,(void*)0xc0000000,query,28));CHECK(!VirtualQueryEx(0,memory,query,28)&&GetLastError()==6);
 U old=0;CHECK(VirtualProtectEx(process,memory,4096,2,&old)==4&&old==4);
 CHECK(ReadProcessMemory(process,memory,output,32,&done)&&done==32);
 CHECK(!WriteProcessMemory(process,memory,source,32,&done)&&GetLastError()==299&&!done);
 CHECK(VirtualProtectEx(process,memory,4096,4,&old)==2&&old==2);
 CHECK(VirtualProtectEx(process,memory+8192,4096,1,&old));
 CHECK(!ReadProcessMemory(process,memory+8000,output,1000,&done)&&GetLastError()==299&&!done);
 CHECK(!ReadProcessMemory(0,memory,output,1,&done)&&GetLastError()==6&&!done);
 CHECK(!ReadProcessMemory(process,memory,(void*)1,1,&done)&&GetLastError()==87&&!done);
 CHECK(!WriteProcessMemory(0,memory,(void*)1,1,&done)&&GetLastError()==87);
 CHECK(ReadProcessMemory(process,(void*)1,output,0,&done)&&!done);
 CHECK(VirtualProtectEx(process,memory,4096,4,0)==4);
 CHECK(!VirtualProtectEx(process,(void*)0xc0000000,1,4,&old)&&GetLastError()==487);
 SetLastError(456);CHECK(VirtualUnlock(memory,0)&&GetLastError()==456);CHECK(!VirtualUnlock((void*)0x3fffff,1)&&GetLastError()==456);
 CHECK(!VirtualUnlock((void*)0x7fffffff,1));
 // Write, execute, rewrite, flush, execute: exercise the translated code cache.
 unsigned char code[]={0xb8,42,0,0,0,0xc3};
 CHECK(WriteProcessMemory(process,memory,code,sizeof code,&done));
 CHECK(FlushInstructionCache(process,memory,sizeof code));
 typedef U (__stdcall *Function)(void);Function fn=(Function)memory;CHECK(fn()==42);
 code[1]=91;CHECK(WriteProcessMemory(process,memory,code,sizeof code,0));CHECK(FlushInstructionCache(process,0,0));CHECK(fn()==91);
 SetLastError(789);CHECK(FlushInstructionCache(0,(void*)~0u,~0u)&&GetLastError()==789);
 CHECK(VirtualFree(memory,0,0x8000));ExitProcess(0);
}
