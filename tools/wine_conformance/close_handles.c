/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"

static void close_real(const char *name, HANDLE handle)
{
    BOOL result;
    DWORD error;
    ok(handle && handle != INVALID_HANDLE_VALUE,"%s created, error=%lu\n",name,GetLastError());
    if(!handle || handle == INVALID_HANDLE_VALUE)return;
    SetLastError(0xdeadbeef);
    result=CloseHandle(handle);error=GetLastError();
    ok(result && !error,"%s close=%u error=%08lx\n",name,result,error);
    SetLastError(0xdeadbeef);
    result=CloseHandle(handle);error=GetLastError();
    ok(!result && error==ERROR_INVALID_HANDLE,"%s repeated close=%u error=%08lx\n",name,result,error);
}

START_TEST(close_handles)
{
    HANDLE read_pipe=NULL,write_pipe=NULL;
    BOOL result;
    DWORD error;
    close_real("event",CreateEventA(NULL,FALSE,FALSE,NULL));
    close_real("mutex",CreateMutexA(NULL,FALSE,NULL));
    close_real("semaphore",CreateSemaphoreA(NULL,0,1,NULL));
    close_real("mapping",CreateFileMappingA(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,4096,NULL));
    close_real("file",CreateFileA("CLOSE98.TMP",GENERIC_READ|GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL));
    DeleteFileA("CLOSE98.TMP");
    close_real("CONFIGMG",CreateFileA("\\\\.\\CONFIGMG",GENERIC_READ|GENERIC_WRITE,3,NULL,OPEN_EXISTING,0,NULL));
    close_real("VWIN32",CreateFileA("\\\\.\\VWIN32",0,0,NULL,OPEN_EXISTING,0,NULL));
    if(CreatePipe(&read_pipe,&write_pipe,NULL,0)){
        close_real("pipe read",read_pipe);
        close_real("pipe write",write_pipe);
    }else ok(0,"create pipe error=%lu\n",GetLastError());
    SetLastError(0xdeadbeef);
    result=CloseHandle(GetCurrentProcess());error=GetLastError();
    ok(result && error==0xdeadbeef,"process pseudo close=%u error=%08lx\n",result,error);
    SetLastError(0xdeadbeef);
    result=CloseHandle(GetCurrentThread());error=GetLastError();
    ok(result && error==0xdeadbeef,"thread pseudo close=%u error=%08lx\n",result,error);
}
