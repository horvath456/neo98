/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
static CRITICAL_SECTION section;
static volatile LONG inside, failures, total, timer_calls;
static HANDLE ready;
static void Check(int ok) { if (!ok) InterlockedIncrement(&failures); }
static DWORD WINAPI Worker(void *unused)
{
    (void)unused;
    for (unsigned i=0; i<20000; ++i) {
        EnterCriticalSection(&section); EnterCriticalSection(&section);
        Check(InterlockedIncrement(&inside)==1);
        LONG previous=total;
        for (volatile unsigned j=0;j<80;++j) {}
        total=previous+1;
        Check(InterlockedDecrement(&inside)==0);
        LeaveCriticalSection(&section); LeaveCriticalSection(&section);
    }
    return 0;
}
static DWORD WINAPI Waiter(void *unused)
{
    (void)unused; SetEvent(ready);
    EnterCriticalSection(&section); total++; LeaveCriticalSection(&section);
    return 0;
}
static void CALLBACK Timer(HWND window,UINT message,UINT_PTR id,DWORD time)
{
    (void)window;(void)message;(void)id;(void)time;
    EnterCriticalSection(&section); ++timer_calls; LeaveCriticalSection(&section);
}
int main(int argc,char **argv)
{
    unsigned count=argc>1?(unsigned)atoi(argv[1]):0;
    InitializeCriticalSection(&section);
    /* Resolve both gates before measuring the loop. LastError must survive. */
    EnterCriticalSection(&section); LeaveCriticalSection(&section);
    SetLastError(0x7654);
    for(unsigned i=0;i<count;++i) {
        EnterCriticalSection(&section);EnterCriticalSection(&section);
        LeaveCriticalSection(&section);LeaveCriticalSection(&section);
    }
    Check(GetLastError()==0x7654);
    if(argc>2) {
        HANDLE threads[4];
        for(unsigned i=0;i<4;++i) {threads[i]=CreateThread(NULL,0,Worker,NULL,0,NULL);Check(threads[i]!=NULL);}
        for(unsigned i=0;i<4;++i) {Check(WaitForSingleObject(threads[i],60000)==WAIT_OBJECT_0);CloseHandle(threads[i]);}
        Check(total==80000 && inside==0);
        ready=CreateEventA(NULL,FALSE,FALSE,NULL);
        /* Force the slow final leave and a real waiting owner transfer. */
        EnterCriticalSection(&section);
        HANDLE waiter=CreateThread(NULL,0,Waiter,NULL,0,NULL);
        Check(WaitForSingleObject(ready,5000)==WAIT_OBJECT_0);Sleep(50);
        Check(total==80000);LeaveCriticalSection(&section);
        Check(WaitForSingleObject(waiter,5000)==WAIT_OBJECT_0);Check(total==80001);
        CloseHandle(waiter);CloseHandle(ready);
        UINT_PTR timer=SetTimer(NULL,0,1,Timer);
        for(unsigned i=0;i<100;++i) {
            MSG msg;EnterCriticalSection(&section);
            Sleep(2);
            while(PeekMessageA(&msg,NULL,0,0,PM_REMOVE)) DispatchMessageA(&msg);
            LeaveCriticalSection(&section);
        }
        KillTimer(NULL,timer);Check(timer_calls>0);
    }
    DeleteCriticalSection(&section);
    printf("CRITICAL_LEAF count=%u total=%ld callbacks=%ld failures=%ld\n",count,total,timer_calls,failures);
    return failures?1:0;
}
