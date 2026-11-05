/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static HHOOK local_hook,global_hook;
static unsigned phase,block,local_count,global_count,event_count;
static DWORD owner;
static HWND window;
static LRESULT CALLBACK local_proc(int code,WPARAM wp,LPARAM lp)
{
    if(code==HCBT_SYSCOMMAND){
        ++local_count;
        trace("HOOK_DELIVERY cbt phase=%u kind=local command=%lx param=%lx owner=%u\n",phase,(unsigned long)wp,(unsigned long)lp,GetCurrentThreadId()==owner);
        if(block)return 1;
    }
    return CallNextHookEx(local_hook,code,wp,lp);
}
static LRESULT CALLBACK global_proc(int code,WPARAM wp,LPARAM lp)
{
    if(code==HCBT_SYSCOMMAND){
        ++global_count;
        trace("HOOK_DELIVERY cbt phase=%u kind=global command=%lx param=%lx owner=%u\n",phase,(unsigned long)wp,(unsigned long)lp,GetCurrentThreadId()==owner);
        if(block)return 1;
    }
    return CallNextHookEx(global_hook,code,wp,lp);
}
static void CALLBACK event_proc(HWINEVENTHOOK hook,DWORD event,HWND hwnd,LONG object,LONG child,DWORD thread,DWORD time)
{
    ++event_count;
    trace("HOOK_DELIVERY event phase=%u event=%lx object=%ld child=%ld owner=%u source=%u\n",phase,(unsigned long)event,object,child,GetCurrentThreadId()==owner,thread==owner);
}
static DWORD WINAPI commands(void *unused)
{
    DefWindowProcA(window,WM_SYSCOMMAND,SC_NEXTWINDOW,0);
    DefWindowProcA(window,WM_SYSCOMMAND,SC_PREVWINDOW,0);
    return 0;
}
static DWORD WINAPI notify(void *unused)
{
    NotifyWinEvent(EVENT_OBJECT_CREATE,window,OBJID_CARET,7);
    return 0;
}
static void worker(LPTHREAD_START_ROUTINE proc)
{
    DWORD id;HANDLE t=CreateThread(NULL,0,proc,NULL,0,&id);
    ok(t!=NULL,"worker\n");
    if(t){ok(WaitForSingleObject(t,5000)==WAIT_OBJECT_0,"worker completion\n");CloseHandle(t);}
}
START_TEST(hook_delivery)
{
    HHOOK bad;HWINEVENTHOOK event_hook;MSG msg;unsigned i;
    static const unsigned syscommands[]={SC_NEXTWINDOW,SC_PREVWINDOW,SC_CLOSE,SC_MINIMIZE,SC_MAXIMIZE,SC_RESTORE};
    owner=GetCurrentThreadId();
    window=CreateWindowA("STATIC","hooks",WS_POPUP,0,0,10,10,NULL,NULL,GetModuleHandleA(NULL),NULL);
    ok(window!=NULL,"window\n");
    SetLastError(0xdeadbeef);bad=SetWindowsHookExA(WH_CBT,NULL,GetModuleHandleA(NULL),owner);
    trace("HOOK_DELIVERY invalid accepted=%u error=%lx\n",bad!=NULL,(unsigned long)GetLastError());
    if(bad)UnhookWindowsHookEx(bad);
    local_hook=SetWindowsHookExA(WH_CBT,local_proc,GetModuleHandleA(NULL),owner);
    global_hook=SetWindowsHookExA(WH_CBT,global_proc,GetModuleHandleA(NULL),0);
    ok(local_hook&&global_hook,"hooks\n");
    phase=1;block=0;DefWindowProcA(window,WM_SYSCOMMAND,SC_NEXTWINDOW,0);
    phase=2;worker(commands);
    phase=3;block=1;
    for(i=0;i<sizeof syscommands/sizeof syscommands[0];++i)DefWindowProcA(window,WM_SYSCOMMAND,syscommands[i],0x12345678);
    trace("HOOK_DELIVERY cbt totals local=%u global=%u\n",local_count,global_count);
    UnhookWindowsHookEx(local_hook);UnhookWindowsHookEx(global_hook);
    while(PeekMessageA(&msg,NULL,0,0,PM_REMOVE))DispatchMessageA(&msg);
    for(i=0;i<2;++i){
        phase=10+i;event_count=0;
        event_hook=SetWinEventHook(EVENT_OBJECT_CREATE,EVENT_OBJECT_NAMECHANGE,i?GetModuleHandleA(NULL):NULL,event_proc,GetCurrentProcessId(),0,i?WINEVENT_INCONTEXT:WINEVENT_OUTOFCONTEXT);
        ok(event_hook!=NULL,"event hook %u\n",i);
        NotifyWinEvent(EVENT_OBJECT_CREATE,window,OBJID_CARET,1);
        NotifyWinEvent(EVENT_OBJECT_SHOW,window,OBJID_CARET,2);
        NotifyWinEvent(EVENT_OBJECT_NAMECHANGE,window,OBJID_CARET,3);
        trace("HOOK_DELIVERY event beforepump mode=%u count=%u\n",i,event_count);
        PeekMessageA(&msg,NULL,0,0,PM_NOREMOVE);
        trace("HOOK_DELIVERY event afterpump mode=%u count=%u\n",i,event_count);
        worker(notify);
        trace("HOOK_DELIVERY event workerbeforepump mode=%u count=%u\n",i,event_count);
        PeekMessageA(&msg,NULL,0,0,PM_NOREMOVE);
        trace("HOOK_DELIVERY event workerafterpump mode=%u count=%u\n",i,event_count);
        UnhookWinEvent(event_hook);
    }
    DestroyWindow(window);
}
