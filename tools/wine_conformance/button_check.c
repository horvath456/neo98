/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static unsigned notifications;
static LRESULT CALLBACK parent_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp)
{
    if(msg==WM_COMMAND&&HIWORD(wp)==BN_CLICKED)++notifications;
    return DefWindowProcA(hwnd,msg,wp,lp);
}
START_TEST(button_check)
{
    WNDCLASSA cls={0};HWND parent;unsigned style,i;
    const unsigned values[]={0,1,2,3,0xffff,0x10000,0};
    cls.lpfnWndProc=parent_proc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="ButtonCheck";
    ok(RegisterClassA(&cls)!=0,"register parent\n");
    parent=CreateWindowA(cls.lpszClassName,"",WS_OVERLAPPEDWINDOW,0,0,300,300,NULL,NULL,cls.hInstance,NULL);
    ok(parent!=NULL,"create parent\n");if(!parent)return;
    for(style=0;style<=11;++style){
        HWND button=CreateWindowA("BUTTON","test",WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,0,0,60,30,parent,(HMENU)100,cls.hInstance,NULL);
        ok(button!=NULL,"create style %u\n",style);if(!button)continue;
        for(i=0;i<sizeof(values)/sizeof(values[0]);++i){
            LRESULT result=SendMessageA(button,BM_SETCHECK,values[i],0);
            trace("BUTTON_CHECK style=%u value=%08x result=%ld check=%ld tab=%u\n",style,values[i],result,SendMessageA(button,BM_GETCHECK,0,0),!!(GetWindowLongA(button,GWL_STYLE)&WS_TABSTOP));
        }
        DestroyWindow(button);
    }
    {
        HWND buttons[4];unsigned stage;
        for(i=0;i<4;++i){buttons[i]=CreateWindowA("BUTTON","radio",WS_CHILD|WS_VISIBLE|BS_AUTORADIOBUTTON|((i%2)==0?WS_GROUP|WS_TABSTOP:0),0,i*30,60,30,parent,(HMENU)(200+i),cls.hInstance,NULL);ok(buttons[i]!=NULL,"create radio %u\n",i);}
        for(stage=0;stage<4;++stage){
            notifications=0;
            if(stage==0){SendMessageA(buttons[0],BM_SETCHECK,1,0);SendMessageA(buttons[2],BM_SETCHECK,1,0);}
            else if(stage==1)SendMessageA(buttons[1],BM_SETCHECK,1,0);
            else SendMessageA(buttons[stage==2?1:3],BM_CLICK,0,0);
            trace("BUTTON_CHECK group stage=%u checks=%ld,%ld,%ld,%ld tabs=%u,%u,%u,%u notifications=%u\n",stage,
                  SendMessageA(buttons[0],BM_GETCHECK,0,0),SendMessageA(buttons[1],BM_GETCHECK,0,0),SendMessageA(buttons[2],BM_GETCHECK,0,0),SendMessageA(buttons[3],BM_GETCHECK,0,0),
                  !!(GetWindowLongA(buttons[0],GWL_STYLE)&WS_TABSTOP),!!(GetWindowLongA(buttons[1],GWL_STYLE)&WS_TABSTOP),!!(GetWindowLongA(buttons[2],GWL_STYLE)&WS_TABSTOP),!!(GetWindowLongA(buttons[3],GWL_STYLE)&WS_TABSTOP),notifications);
        }
    }
    DestroyWindow(parent);
}
