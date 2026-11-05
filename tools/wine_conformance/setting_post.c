/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcA(h,m,w,l);}
START_TEST(setting_post)
{
    WNDCLASSA c={0};HWND h;unsigned mode;MSG msg;
    c.hInstance=GetModuleHandleA(0);c.lpfnWndProc=proc;c.lpszClassName="SettingPostProbe";
    RegisterClassA(&c);h=CreateWindowA(c.lpszClassName,"probe",WS_OVERLAPPED,0,0,10,10,0,0,c.hInstance,0);
    ok(h!=0,"window\n");
    for(mode=0;mode<4;++mode){
        BOOL result;DWORD error;unsigned received=0;
        LPARAM p=(mode&1)?(LPARAM)"Environment":0;
        SetLastError(0xdeadbeef);
        result=PostMessageA(mode&2?HWND_BROADCAST:h,WM_SETTINGCHANGE,0,p);error=GetLastError();
        while(PeekMessageA(&msg,h,WM_SETTINGCHANGE,WM_SETTINGCHANGE,PM_REMOVE))++received;
        trace("SETTING_POST mode=%u result=%u error=%lu received=%u\n",mode,!!result,error,received);
    }
    DestroyWindow(h);UnregisterClassA(c.lpszClassName,c.hInstance);
}
