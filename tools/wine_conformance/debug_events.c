/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include <string.h>
#include "wine/test.h"
START_TEST(debug_events)
{
    int argc;char **argv;char exe[MAX_PATH],command[MAX_PATH+40];
    STARTUPINFOA si={0};PROCESS_INFORMATION pi={0};DEBUG_EVENT event;unsigned n;
    argc=winetest_get_mainargs(&argv);if(argc>2){ExitProcess(42);return;}
    ok(GetDesktopWindow()!=NULL,"load USER32 as in the contracts suite\n");
    ok(GetModuleHandleA("GDI32.DLL")!=NULL,"USER32 loads GDI32\n");
    ok(GetModuleHandleA("ADVAPI32.DLL")!=NULL,"USER32 loads ADVAPI32\n");
    GetModuleFileNameA(NULL,exe,sizeof exe);sprintf(command,"\"%s\" debug_events child",exe);si.cb=sizeof si;
    ok(CreateProcessA(NULL,command,NULL,NULL,FALSE,DEBUG_ONLY_THIS_PROCESS,NULL,NULL,&si,&pi),"start debuggee\n");
    if(!pi.hProcess)return;
    for(n=0;n<64;++n){
        BOOL got=WaitForDebugEvent(&event,10000);ok(got,"receive event\n");if(!got)break;
        trace("DEBUG_EVENT index=%u code=%lu\n",n,event.dwDebugEventCode);
        if(event.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT){
            char name[260]={0};DWORD ptr=0,read=0;
            if(event.u.LoadDll.lpImageName&&ReadProcessMemory(pi.hProcess,event.u.LoadDll.lpImageName,&ptr,4,&read)&&ptr)
                ReadProcessMemory(pi.hProcess,(LPCVOID)ptr,name,sizeof name-1,&read);
            ok(ptr&&name[0],"DLL image name is readable in the debuggee\n");
            trace("DEBUG_DLL index=%u base=%p unicode=%u name=%s\n",n,event.u.LoadDll.lpBaseOfDll,event.u.LoadDll.fUnicode,name);
            if(event.u.LoadDll.hFile)CloseHandle(event.u.LoadDll.hFile);
        }
        if(event.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT&&event.u.CreateProcessInfo.hFile)CloseHandle(event.u.CreateProcessInfo.hFile);
        if(event.dwDebugEventCode==EXCEPTION_DEBUG_EVENT)trace("DEBUG_EXCEPTION code=%08lx first=%lu\n",event.u.Exception.ExceptionRecord.ExceptionCode,event.u.Exception.dwFirstChance);
        ok(ContinueDebugEvent(event.dwProcessId,event.dwThreadId,DBG_CONTINUE),"continue event\n");
        if(event.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT)break;
    }
    if(n==64)TerminateProcess(pi.hProcess,99);
    WaitForSingleObject(pi.hProcess,5000);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
}
