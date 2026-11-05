/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static unsigned active,changing,calculated,changed,painted;
static RECT before,after;
static BOOL before_present,after_present;
static LRESULT CALLBACK window_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp)
{
    if(active&&(msg==WM_WINDOWPOSCHANGING||msg==WM_NCCALCSIZE||msg==WM_WINDOWPOSCHANGED||msg==WM_NCPAINT)){
        HRGN copy=CreateRectRgn(0,0,0,0);RECT box={0};int got=GetWindowRgn(hwnd,copy);
        if(got)GetRgnBox(copy,&box);DeleteObject(copy);
        trace("REGION_EVENT phase=%u message=%04x present=%u box=%ld,%ld,%ld,%ld\n",
            active,msg,got!=0,box.left,box.top,box.right,box.bottom);
        if(msg==WM_WINDOWPOSCHANGING||msg==WM_NCCALCSIZE){
            ok((got!=0)==before_present,"old region visible before apply phase=%u msg=%x\n",active,msg);
            if(got)ok(EqualRect(&box,&before),"old region bounds phase=%u msg=%x\n",active,msg);
        }
        if(msg==WM_WINDOWPOSCHANGED){
            ok((got!=0)==after_present,"new region visible after apply phase=%u\n",active);
            if(got)ok(EqualRect(&box,&after),"new region bounds phase=%u\n",active);
            ++changed;
        }
        if(msg==WM_WINDOWPOSCHANGING)++changing;
        if(msg==WM_NCCALCSIZE)++calculated;
        if(msg==WM_NCPAINT)++painted;
    }
    return DefWindowProcA(hwnd,msg,wp,lp);
}
START_TEST(window_region_events)
{
    WNDCLASSA cls={0};HWND window;unsigned phase;HRGN region;BOOL redraw;
    cls.lpfnWndProc=window_proc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="RegionEvents98";
    cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    ok(RegisterClassA(&cls)!=0,"register region class\n");
    window=CreateWindowA(cls.lpszClassName,"region",WS_OVERLAPPEDWINDOW|WS_VISIBLE,100,100,200,200,NULL,NULL,cls.hInstance,NULL);
    ok(window!=NULL,"create region window\n");if(!window)return;
    UpdateWindow(window);
    for(phase=1;phase<=3;++phase){
        MSG message;while(PeekMessageA(&message,NULL,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageA(&message);}
        before=after;before_present=after_present;
        SetRect(&after,phase==2?30:0,phase==2?30:0,phase==2?160:150,phase==2?160:150);
        after_present=phase!=3;region=after_present?CreateRectRgnIndirect(&after):NULL;
        changing=calculated=changed=painted=0;redraw=phase!=2;active=phase;
        ok(SetWindowRgn(window,region,redraw)!=0,"set region phase=%u\n",phase);
        active=0;
        ok(changing==1&&calculated>=1&&changed==1,"position messages phase=%u: %u/%u/%u\n",phase,changing,calculated,changed);
        ok(redraw?painted>=1:painted==0,"synchronous frame paint phase=%u: %u\n",phase,painted);
    }
    DestroyWindow(window);UnregisterClassA(cls.lpszClassName,cls.hInstance);
}
