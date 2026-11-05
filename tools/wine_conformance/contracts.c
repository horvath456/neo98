/* SPDX-License-Identifier: GPL-2.0-or-later
 * Small end-to-end contracts, qualified on original Windows 98 SE. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "wine/test.h"

static void memory_contract(void)
{
    HGLOBAL h, grown;
    char *p;
    DWORD error;
    h=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,0);
    ok(h!=NULL,"empty moveable handle\n");
    ok(GlobalLock(h)==NULL && GlobalSize(h)==0,"discarded handle has no data\n");
    grown=GlobalReAlloc(h,64,GMEM_MOVEABLE|GMEM_ZEROINIT);
    ok(grown==h,"handle must survive growth\n");
    p=GlobalLock(h);
    ok(p!=NULL,"lock data\n");
    if(p){
        ok(p[0]==0 && p[63]==0,"zero-initialized data\n");
        p[0]=42;
        ok(GlobalLock(h)==p && (GlobalFlags(h)&255)==2,"nested locks\n");
        ok(GlobalUnlock(h) && !GlobalUnlock(h),"unlock count\n");
        grown=GlobalReAlloc(h,4096,GMEM_MOVEABLE|GMEM_ZEROINIT);
        ok(grown==h,"stable handle after relocation\n");
        p=GlobalLock(h);
        ok(p && p[0]==42 && p[4095]==0,"preserve prefix and zero growth\n");
        SetLastError(0);
        ok(GlobalDiscard(h)==NULL,"locked memory cannot be discarded\n");
        error=GetLastError();
        trace("locked GlobalDiscard error=%lu\n",error);
        GlobalUnlock(h);
        ok(GlobalDiscard(h)==h && GlobalSize(h)==0,"discard unlocked data\n");
    }
    ok(GlobalFree(h)==NULL,"free discarded handle\n");
    SetLastError(0);
    ok(GlobalLock(h)==NULL && GetLastError()==ERROR_INVALID_HANDLE,"stale handle rejected\n");
}

static void file_contract(void)
{
    HANDLE file,map;
    DWORD done=777;
    char *view, bytes[4]={0};
    file=CreateFileA("contract.bin",GENERIC_READ|GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);
    ok(file!=INVALID_HANDLE_VALUE,"create file\n");
    if(file==INVALID_HANDLE_VALUE)return;
    ok(WriteFile(file,"abc",4,&done,NULL) && done==4,"write seed\n");
    map=CreateFileMappingA(file,NULL,PAGE_WRITECOPY,0,4,NULL);
    ok(map!=NULL,"copy-on-write mapping\n");
    if(map){
        view=MapViewOfFile(map,FILE_MAP_COPY,0,0,4);
        ok(view && !memcmp(view,"abc",4),"mapping contains file bytes\n");
        if(view){view[0]='x';ok(UnmapViewOfFile(view),"unmap private copy\n");}
        CloseHandle(map);
    }
    SetFilePointer(file,0,NULL,FILE_BEGIN);
    ok(ReadFile(file,bytes,4,&done,NULL) && !memcmp(bytes,"abc",4),"private copy must not overwrite file\n");
    CloseHandle(file);
    file=CreateFileA("contract.bin",GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    ok(file!=INVALID_HANDLE_VALUE,"readonly handle\n");
    SetLastError(0);
    map=CreateFileMappingA(file,NULL,PAGE_READWRITE,0,4,NULL);
    ok(map==NULL && (GetLastError()==ERROR_INVALID_PARAMETER || GetLastError()==ERROR_ACCESS_DENIED),
       "mapping must not gain write access\n");
    if(map)CloseHandle(map);
    CloseHandle(file);
    ok(DeleteFileA("contract.bin"),"delete file after mapping closure\n");
    file=CreateFileA("NUL",GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
    ok(file!=INVALID_HANDLE_VALUE && GetFileType(file)==FILE_TYPE_CHAR,"NUL is a character device\n");
    if(file!=INVALID_HANDLE_VALUE){
        ok(WriteFile(file,"abc",3,&done,NULL)&&done==3,"NUL accepts output\n");
        ok(ReadFile(file,bytes,4,&done,NULL)&&done==0,"NUL input ends immediately\n");
        ok(CloseHandle(file),"close NUL\n");
    }
}

static void clipboard_contract(void)
{
    HWND owner=CreateWindowExA(0,"STATIC","contract",0,0,0,32,32,NULL,NULL,NULL,NULL);
    HGLOBAL memory,copy;
    char *text;
    ok(owner!=NULL,"clipboard owner window\n");
    if(!owner)return;
    if(OpenClipboard(owner)){
        ok(EmptyClipboard(),"empty clipboard\n");
        memory=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,32);
        text=GlobalLock(memory);
        ok(text!=NULL,"lock movable clipboard memory\n");
        if(text){
            strcpy(text,"movable clipboard");GlobalUnlock(memory);
            ok(SetClipboardData(CF_TEXT,memory)!=NULL,"publish movable clipboard memory\n");
            copy=GetClipboardData(CF_TEXT);text=copy?GlobalLock(copy):NULL;
            ok(text && !strcmp(text,"movable clipboard"),"clipboard preserves data behind handle\n");
            if(text)GlobalUnlock(copy);
        }
        ok(CloseClipboard(),"close clipboard\n");
    }else ok(0,"open clipboard\n");
    ok(DestroyWindow(owner),"destroy clipboard owner\n");
}

static BOOL WINAPI control_handler(DWORD event) { return TRUE; }

static void debugger_contract(void)
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DEBUG_EVENT event;
    char exe[MAX_PATH],command[MAX_PATH+40];
    unsigned creates=0,exits=0,events=0;
    DWORD code=0;
    BOOL started;
    GetModuleFileNameA(NULL,exe,sizeof exe);
    sprintf(command,"\"%s\" contracts debug-child",exe);
    memset(&si,0,sizeof si);si.cb=sizeof si;memset(&pi,0,sizeof pi);
    ok(SetConsoleCtrlHandler(control_handler,TRUE),"register parent console handler\n");
    started=CreateProcessA(NULL,command,NULL,NULL,FALSE,DEBUG_ONLY_THIS_PROCESS,NULL,NULL,&si,&pi);
    ok(started,"start debuggee: %lu\n",GetLastError());
    if(!started)return;
    while(events++<128){
        BOOL got=WaitForDebugEvent(&event,10000);
        ok(got,"receive debug event: %lu\n",GetLastError());
        if(!got)break;
        ok(event.dwProcessId==pi.dwProcessId,"event belongs to debuggee\n");
        if(event.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT){
            ++creates;
            ok(event.u.CreateProcessInfo.lpBaseOfImage!=NULL,"debuggee image base\n");
            ok(GetExitCodeProcess(pi.hProcess,&code)&&code==STILL_ACTIVE,"debuggee held at create event\n");
            if(event.u.CreateProcessInfo.hFile)CloseHandle(event.u.CreateProcessInfo.hFile);
        }
        if(event.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT && event.u.LoadDll.hFile)CloseHandle(event.u.LoadDll.hFile);
        if(event.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT){
            ++exits;ok(event.u.ExitProcess.dwExitCode==42,"exit event retains result\n");
        }
        ok(!ContinueDebugEvent(0,0,DBG_CONTINUE),"invalid continuation rejected\n");
        ok(ContinueDebugEvent(event.dwProcessId,event.dwThreadId,DBG_CONTINUE),"continue actual event\n");
        if(exits)break;
    }
    ok(creates==1 && exits==1,"one create and one exit event\n");
    if(!exits)TerminateProcess(pi.hProcess,99);
    ok(WaitForSingleObject(pi.hProcess,5000)==WAIT_OBJECT_0,"debuggee reaped\n");
    ok(GetExitCodeProcess(pi.hProcess,&code)&&code==42,"process exit code\n");
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    ok(SetConsoleCtrlHandler(control_handler,FALSE),"child must not remove parent's console handler\n");
}

START_TEST(contracts)
{
    int argc;char **argv;
    argc=winetest_get_mainargs(&argv);
    if(argc>2 && !strcmp(argv[2],"debug-child")){
        ok(!SetConsoleCtrlHandler(control_handler,FALSE),"console handlers belong to each process\n");
        ok(IsDebuggerPresent(),"child sees attached debugger\n");
        fflush(stdout);ExitProcess(42);
    }
    memory_contract();
    file_contract();
    clipboard_contract();
    debugger_contract();
}
