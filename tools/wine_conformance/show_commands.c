/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static HWND observed;
static unsigned shows,show_value,changing,changed,moves,sizes,flags_before,flags_after;
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l)
{
    if(h==observed){
        if(m==WM_SHOWWINDOW){++shows;show_value=(unsigned)w;}
        if(m==WM_WINDOWPOSCHANGING){++changing;flags_before=((WINDOWPOS*)l)->flags;}
        if(m==WM_WINDOWPOSCHANGED){++changed;flags_after=((WINDOWPOS*)l)->flags;}
        if(m==WM_MOVE)++moves;
        if(m==WM_SIZE)++sizes;
    }
    return DefWindowProcA(h,m,w,l);
}
START_TEST(show_commands)
{
    WNDCLASSA cls;HWND parent,h;unsigned mode,visible,i,round;
    static const unsigned cmds[]={SW_SHOW,SW_SHOWNA,SW_SHOWNOACTIVATE,SW_FORCEMINIMIZE};
    memset(&cls,0,sizeof cls);cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="ShowCommands";
    ok(RegisterClassA(&cls)!=0,"class\n");
    for(mode=0;mode<3;++mode){
        parent=mode?CreateWindowA(cls.lpszClassName,"parent",WS_OVERLAPPEDWINDOW|(mode==1?WS_VISIBLE:0),30,30,250,200,NULL,NULL,cls.hInstance,NULL):NULL;
        if(mode)ok(parent!=NULL,"parent %u\n",mode);
        for(visible=0;visible<2;++visible)for(i=0;i<sizeof cmds/sizeof cmds[0];++i){
            h=CreateWindowA(cls.lpszClassName,"show",(mode?WS_CHILD:WS_OVERLAPPEDWINDOW)|(visible?WS_VISIBLE:0),10,10,100,80,parent,NULL,cls.hInstance,NULL);
            ok(h!=NULL,"window %u %u %u\n",mode,visible,i);observed=h;
            for(round=0;round<2;++round){
                BOOL result;shows=changing=changed=moves=sizes=flags_before=flags_after=0;show_value=99;
                result=ShowWindow(h,cmds[i]);
                trace("SHOW_COMMAND mode=%u initial=%u cmd=%u round=%u result=%u style=%lx shown=%u show=%u/%u pos=%u/%x/%u/%x move=%u size=%u\n",mode,visible,cmds[i],round,!!result,(unsigned long)(GetWindowLongA(h,GWL_STYLE)&(WS_VISIBLE|WS_MINIMIZE|WS_MAXIMIZE)),!!IsWindowVisible(h),shows,show_value,changing,flags_before,changed,flags_after,moves,sizes);
            }
            observed=NULL;DestroyWindow(h);
        }
        if(parent)DestroyWindow(parent);
    }
}
