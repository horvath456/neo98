/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static unsigned veto,variant,events,command,hook_style;
static LRESULT CALLBACK hook(int code,WPARAM wp,LPARAM lp)
{
    if(code==HCBT_MINMAX){char cls[64];GetClassNameA((HWND)wp,cls,sizeof cls);if(!lstrcmpA(cls,"CreateMinMax98")){++events;command=(unsigned)lp;hook_style=GetWindowLongA((HWND)wp,GWL_STYLE)&0x31000000;return veto;}}
    return CallNextHookEx(NULL,code,wp,lp);
}
START_TEST(create_minmax)
{
    WNDCLASSA cls={0};HHOOK h;unsigned visible;RECT old_work,work={0,0,800,572};
    ok(SystemParametersInfoA(SPI_GETWORKAREA,0,&old_work,0),"read work area\n");
    ok(SystemParametersInfoA(SPI_SETWORKAREA,0,&work,0),"set work area\n");
    cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="CreateMinMax98";ok(RegisterClassA(&cls)!=0,"class\n");
    h=SetWindowsHookExA(WH_CBT,hook,NULL,GetCurrentThreadId());ok(h!=NULL,"hook\n");
    for(veto=0;veto<2;++veto)for(variant=0;variant<2;++variant)for(visible=0;visible<2;++visible){
        HWND window;RECT r={0};events=0;command=99;hook_style=0;
        window=CreateWindowA(cls.lpszClassName,"minmax",WS_OVERLAPPEDWINDOW|(variant?WS_MAXIMIZE:WS_MINIMIZE)|(visible?WS_VISIBLE:0),40,50,200,150,NULL,NULL,cls.hInstance,NULL);
        ok(window!=NULL,"create\n");GetWindowRect(window,&r);
        trace("CREATE_MINMAX veto=%u max=%u visible=%u events=%u command=%u hookstyle=%x style=%lx rect=%ld,%ld,%ld,%ld\n",veto,variant,visible,events,command,hook_style,GetWindowLongA(window,GWL_STYLE)&0x31000000,r.left,r.top,r.right,r.bottom);fflush(stdout);
        DestroyWindow(window);
    }
    UnhookWindowsHookEx(h);
    ok(SystemParametersInfoA(SPI_SETWORKAREA,0,&old_work,0),"restore work area\n");
}
