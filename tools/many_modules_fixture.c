/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* Original Win98 comparison: DLL lifetime beyond the former 32-module limit. */
#include <windows.h>
static HANDLE output;
static void row(const char *name, DWORD result, DWORD error) {
    char text[128]; DWORD done;
    int n=wsprintfA(text,"%s result=%08lx error=%lu\r\n",name,result,error);
    WriteFile(output,text,n,&done,0);
}
void mainCRTStartup(void) {
    output=CreateFileA("C:\\MODLOCK.TXT",GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);
    /* Cross the former 32-module limit before a dynamic load/unload. */
    HMODULE keep[40]; char path[64];
    for(int i=0;i<40;++i) {
        wsprintfA(path,"C:\\KEEP%02d.DLL",i);
        CopyFileA("C:\\LOCKSRC.DLL",path,FALSE);
        keep[i]=LoadLibraryA(path);
        if(!keep[i]) { row("preload-failed",i,GetLastError()); ExitProcess(2); }
    }
    DeleteFileA("C:\\LOCKED.DLL"); DeleteFileA("C:\\MOVED.DLL");
    CopyFileA("C:\\LOCKSRC.DLL","C:\\LOCKED.DLL",FALSE);
    HMODULE a=LoadLibraryA("C:\\LOCKED.DLL"); row("load",a!=0,GetLastError());
    HMODULE b=LoadLibraryA("C:\\LOCKED.DLL"); row("load-again",a==b,GetLastError());
    FreeLibrary(a);
    row("module-after-one-free",GetModuleHandleA("C:\\LOCKED.DLL")!=0,0);
    FreeLibrary(b);
    row("module-after-final-free",GetModuleHandleA("C:\\LOCKED.DLL")!=0,0);
    SetLastError(0); BOOL r=DeleteFileA("C:\\LOCKED.DLL"); row("delete-unloaded",r,GetLastError());
    for(int cycle=0;cycle<20;++cycle) {
        CopyFileA("C:\\LOCKSRC.DLL","C:\\LOCKED.DLL",FALSE);
        a=LoadLibraryA("C:\\LOCKED.DLL");
        if(!a) { row("reload-failed",cycle,GetLastError()); ExitProcess(3); }
        FreeLibrary(a);
        if(GetModuleHandleA("C:\\LOCKED.DLL")) { row("reload-retained",cycle,0); break; }
        if(cycle==19)row("reload-cycles",20,0);
    }
    for(int i=39;i>=0;--i)FreeLibrary(keep[i]);
    CloseHandle(output); ExitProcess(0);
}
