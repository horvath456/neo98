/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
// Install the imported TLS DLL before starting the C++ process. Win98 only
// allocates static TLS for modules in the initial import graph.
#include <windows.h>
#include <stdio.h>
static BOOL Resource(int id, const char *path)
{
    HRSRC r=FindResourceA(NULL,MAKEINTRESOURCEA(id),RT_RCDATA);
    if(!r)return FALSE;
    DWORD bytes=SizeofResource(NULL,r),done=0;
    const void *data=LockResource(LoadResource(NULL,r));
    HANDLE f=CreateFileA(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);
    if(f==INVALID_HANDLE_VALUE)return FALSE;
    BOOL ok=WriteFile(f,data,bytes,&done,NULL)&&done==bytes;CloseHandle(f);return ok;
}
int main(void)
{
    if(!Resource(1,"C:\\CXREMAIN.EXE")||!Resource(2,"C:\\TLS4.DLL"))return 2;
    STARTUPINFOA start={0};PROCESS_INFORMATION process={0};DWORD code=99;start.cb=sizeof start;
    char command[]="C:\\CXREMAIN.EXE";
    if(!CreateProcessA(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&start,&process))return 3;
    if(WaitForSingleObject(process.hProcess,60000)!=WAIT_OBJECT_0)return 4;
    GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hThread);CloseHandle(process.hProcess);
    DeleteFileA("C:\\CXREMAIN.EXE");DeleteFileA("C:\\TLS4.DLL");
    printf("CXXLAUNCH child=%lu\n",(unsigned long)code);return code;
}
