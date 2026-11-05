/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
static int failures;
static DWORD slot;
static void Check(int id,int ok){if(!ok){++failures;printf("FAIL %d\n",id);}}
static DWORD WINAPI Thread(void *arg)
{
    int want=(int)(ULONG_PTR)arg; errno=want;
    Check(10,TlsSetValue(slot,arg));
    for(int i=0;i<100;++i){Check(11,errno==want);Check(12,TlsGetValue(slot)==arg);Sleep(0);}
    return errno;
}
int main(int argc,char **argv)
{
    int count=argc>1?atoi(argv[1]):0;DWORD code;HANDLE a,b;
    void *(WINAPI *get)(DWORD)=(void *)GetProcAddress(GetModuleHandleA("KERNEL32.DLL"),"TlsGetValue");
    errno=17;slot=TlsAlloc();Check(1,slot<80);Check(2,TlsSetValue(slot,(void *)0x12345678));
    for(int i=0;i<3;++i){SetLastError(99);Check(3,TlsGetValue(slot)==(void *)0x12345678);Check(4,GetLastError()==0);}
    Check(5,get&&get(slot)==(void *)0x12345678);
    SetLastError(99);Check(6,get&&get(80)==NULL);Check(7,GetLastError()==ERROR_INVALID_PARAMETER);
    a=CreateThread(NULL,0,Thread,(void *)23,0,NULL);b=CreateThread(NULL,0,Thread,(void *)41,0,NULL);
    Check(8,a&&b);if(a){WaitForSingleObject(a,INFINITE);GetExitCodeThread(a,&code);Check(9,code==23);CloseHandle(a);}
    if(b){WaitForSingleObject(b,INFINITE);GetExitCodeThread(b,&code);Check(13,code==41);CloseHandle(b);}
    Check(14,errno==17);Check(15,TlsGetValue(slot)==(void *)0x12345678);
    for(int i=0;i<count;++i) errno+=i&1;
    Check(16,errno==17+count/2);TlsFree(slot);
    printf("ERRNO_LEAF count=%d failures=%d\n",count,failures);return failures;
}
