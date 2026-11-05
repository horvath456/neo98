/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static HWND top,child;static HANDLE ready;static DWORD worker_id;
static unsigned count,sequence[16],wrong_thread;
static LRESULT CALLBACK procedure(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp)
{
    if(msg==WM_DESTROY||msg==WM_NCDESTROY){
        if(GetCurrentThreadId()!=worker_id)++wrong_thread;
        if(count<16)sequence[count++]=(hwnd==top?10:20)+(msg==WM_DESTROY?1:2);
    }
    return DefWindowProcA(hwnd,msg,wp,lp);
}
static DWORD WINAPI worker(void *arg)
{
    unsigned mode=(unsigned)(ULONG_PTR)arg;
    worker_id=GetCurrentThreadId();
    top=CreateWindowA("ThreadWindows","top",WS_OVERLAPPED,10,10,100,100,NULL,NULL,GetModuleHandleA(NULL),NULL);
    child=CreateWindowA("ThreadWindows","child",WS_CHILD,0,0,30,30,top,NULL,GetModuleHandleA(NULL),NULL);
    ok(top!=NULL&&child!=NULL,"worker windows\n");
    SetEvent(ready);
    if(mode==2)Sleep(INFINITE);
    if(mode==1)ExitThread(18);
    return 17;
}
START_TEST(thread_windows)
{
    WNDCLASSA cls={0};unsigned mode;
    cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="ThreadWindows";
    ok(RegisterClassA(&cls)!=0,"register\n");
    for(mode=0;mode<3;++mode){
        HANDLE thread;DWORD id,code=0,wait;char order[256]="";unsigned i;
        count=wrong_thread=0;top=child=NULL;
        ready=CreateEventA(NULL,FALSE,FALSE,NULL);ok(ready!=NULL,"ready event\n");
        thread=CreateThread(NULL,0,worker,(void*)(ULONG_PTR)mode,0,&id);ok(thread!=NULL,"thread\n");
        if(!thread){CloseHandle(ready);continue;}
        wait=WaitForSingleObject(ready,5000);ok(wait==WAIT_OBJECT_0,"worker setup %lu\n",wait);
        if(mode==2)ok(TerminateThread(thread,19),"terminate\n");
        wait=WaitForSingleObject(thread,5000);ok(wait==WAIT_OBJECT_0,"worker exit %lu\n",wait);
        ok(GetExitCodeThread(thread,&code),"exit code\n");
        for(i=0;i<count&&i<16;++i){char number[16];sprintf(number,"%s%u",i?",":"",sequence[i]);strcat(order,number);}
        trace("THREAD_WINDOWS mode=%u code=%lu top=%u child=%u callbacks=%u wrongthread=%u order=%s\n",mode,code,IsWindow(top),IsWindow(child),count,wrong_thread,order);
        CloseHandle(thread);CloseHandle(ready);
    }
}
