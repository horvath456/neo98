// SPDX-License-Identifier: GPL-2.0-or-later
#include "toolhelp_limits_imports.h"
#define CHECK(x) do {if(!(x))ExitProcess(__LINE__);}while(0)
static U handles[512];
void mainCRTStartup(void){
    U pid=GetCurrentProcessId(),entry[137],duplicate=0,count=0;
    for(U inherit=0;inherit<2;++inherit){
        U flags=inherit?0x80000000u:0;
        CHECK(CreateToolhelp32Snapshot(flags,0)==~0u);CHECK(GetLastError()==87);
        CHECK(CreateToolhelp32Snapshot(flags,0xdeadbeef)==~0u);CHECK(GetLastError()==87);
        U h=CreateToolhelp32Snapshot(flags|2,0);CHECK(h!=~0u);
        entry[0]=296;CHECK(Process32First(h,(U)entry));CHECK(entry[2]==pid);CHECK(CloseHandle(h));
    }
    for(;count<512;++count){
        U h=CreateToolhelp32Snapshot(15,0);
        if(h==~0u){CHECK(GetLastError()==8);break;}
        handles[count]=h;
    }
    CHECK(count>64&&count<=512);
    CHECK(CreateToolhelp32Snapshot(15,0)==~0u);CHECK(GetLastError()==8);
    // Flag validation precedes even handle/resource exhaustion.
    CHECK(CreateToolhelp32Snapshot(0,0)==~0u);CHECK(GetLastError()==87);
    CHECK(CreateToolhelp32Snapshot(0x80000000u,0)==~0u);CHECK(GetLastError()==87);
    U old=handles[count-1];CHECK(CloseHandle(old));--count;
    CHECK(DuplicateHandle(GetCurrentProcess(),handles[0],GetCurrentProcess(),(U)&duplicate,0,0,2));
    CHECK(CloseHandle(handles[0]));handles[0]=duplicate;
    for(U i=0;i<count;++i){
        entry[0]=296;CHECK(Process32First(handles[i],(U)entry));CHECK(entry[2]==pid);
        entry[0]=28;CHECK(Thread32First(handles[i],(U)entry));CHECK(entry[3]==pid);
        entry[0]=548;CHECK(Module32First(handles[i],(U)entry));CHECK(entry[2]==pid);
        entry[0]=16;CHECK(Heap32ListFirst(handles[i],(U)entry));CHECK(entry[1]==pid);
    }
    entry[0]=296;CHECK(!Process32First(old,(U)entry));CHECK(GetLastError()==6);
    for(U i=0;i<count;i+=2){CHECK(CloseHandle(handles[i]));handles[i]=CreateToolhelp32Snapshot(2,0);CHECK(handles[i]!=~0u);}
    for(U i=0;i<count;++i)CHECK(CloseHandle(handles[i]));
    ExitProcess(0);
}
