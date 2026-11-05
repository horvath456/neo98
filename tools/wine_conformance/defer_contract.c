/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
#define SENTINEL 0x12345678
#define FLAGS (SWP_NOZORDER|SWP_NOACTIVATE)
static void outcome(const char *name,BOOL result)
{ DWORD error=GetLastError();trace("DEFER_RESULT name=%s result=%u error=%08lx\n",name,!!result,error); }
static void position(const char *name,HWND window)
{ RECT r={0};BOOL result=GetWindowRect(window,&r);trace("DEFER_RECT name=%s result=%u rect=%ld,%ld,%ld,%ld\n",name,!!result,r.left,r.top,r.right,r.bottom); }
START_TEST(defer_contract)
{
    HWND a,b;HDWP batch,retry;BOOL result;unsigned i;
    WNDCLASSA cls={0};cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="Neo98DeferContract";
    ok(RegisterClassA(&cls)!=0,"register ANSI class\n");
    a=CreateWindowA(cls.lpszClassName,"a",WS_POPUP,10,20,100,80,NULL,NULL,cls.hInstance,NULL);
    b=CreateWindowA(cls.lpszClassName,"b",WS_POPUP,30,40,100,80,NULL,NULL,cls.hInstance,NULL);
    ok(a&&b,"create real ANSI windows\n");if(!a||!b)return;
    SetLastError(SENTINEL);result=EndDeferWindowPos(NULL);outcome("end-null",result);
    SetLastError(SENTINEL);result=EndDeferWindowPos((HDWP)-1);outcome("end-invalid",result);
    SetLastError(SENTINEL);batch=BeginDeferWindowPos(-1);outcome("begin-negative",batch!=NULL);
    SetLastError(SENTINEL);batch=DeferWindowPos(NULL,a,NULL,0,0,10,10,FLAGS);outcome("defer-null-batch",batch!=NULL);
    for(i=0;i<2;++i){
        batch=BeginDeferWindowPos(1);ok(batch!=NULL,"begin error-recovery batch\n");
        SetLastError(SENTINEL);retry=DeferWindowPos(batch,i?(HWND)0xdeadbeef:NULL,NULL,0,0,10,10,FLAGS);
        outcome(i?"invalid-window":"null-window",retry!=NULL);
        SetLastError(SENTINEL);retry=DeferWindowPos(batch,a,NULL,20,30,120,90,FLAGS);
        outcome(i?"retry-invalid-window":"retry-null-window",retry!=NULL);
        SetLastError(SENTINEL);result=EndDeferWindowPos(retry?retry:batch);
        outcome(i?"end-invalid-window":"end-null-window",result);
    }
    batch=BeginDeferWindowPos(1);
    SetLastError(SENTINEL);retry=DeferWindowPos(batch,a,(HWND)0xdeadbeef,50,60,120,90,SWP_NOACTIVATE);
    outcome("invalid-after",retry!=NULL);
    SetLastError(SENTINEL);result=EndDeferWindowPos(retry?retry:batch);outcome("end-invalid-after",result);
    position("after-invalid-after",a);
    batch=BeginDeferWindowPos(0);ok(batch!=NULL,"begin growable batch\n");
    batch=DeferWindowPos(batch,a,NULL,70,80,150,110,FLAGS);ok(batch!=NULL,"first valid position\n");
    batch=DeferWindowPos(batch,b,NULL,90,100,160,120,FLAGS);ok(batch!=NULL,"second valid position\n");
    batch=DeferWindowPos(batch,a,NULL,110,120,999,999,FLAGS|SWP_NOSIZE);ok(batch!=NULL,"merge duplicate\n");
    SetLastError(SENTINEL);result=EndDeferWindowPos(batch);outcome("end-valid",result);ok(result,"apply valid batch\n");
    position("merged-a",a);position("moved-b",b);
    SetLastError(SENTINEL);result=EndDeferWindowPos(batch);outcome("end-stale",result);
    batch=BeginDeferWindowPos(1);batch=DeferWindowPos(batch,b,NULL,0,0,10,10,FLAGS);DestroyWindow(b);
    SetLastError(SENTINEL);result=EndDeferWindowPos(batch);outcome("end-destroyed-window",result);
    DestroyWindow(a);UnregisterClassA(cls.lpszClassName,cls.hInstance);
}
