/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static BOOL recording;
static HWND observed;
static unsigned count,messages[16],flags[16];
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l)
{
    if(recording && m==WM_NCCREATE && !observed)observed=h;
    if(recording && h==observed && (m==WM_WINDOWPOSCHANGING || m==WM_WINDOWPOSCHANGED)){
        if(count<16){messages[count]=m;flags[count]=((WINDOWPOS*)l)->flags;}
        ++count;
    }
    return DefWindowProcA(h,m,w,l);
}
START_TEST(create_position)
{
    WNDCLASSA cls={0};HWND parent;unsigned kind,maximize,visible,i;RECT old_work,work={0,0,800,572};
    ok(SystemParametersInfoA(SPI_GETWORKAREA,0,&old_work,0),"read work area\n");
    ok(SystemParametersInfoA(SPI_SETWORKAREA,0,&work,0),"set work area\n");
    cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="CreatePosition98";
    ok(RegisterClassA(&cls)!=0,"class\n");
    parent=CreateWindowA(cls.lpszClassName,"parent",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,500,400,NULL,NULL,cls.hInstance,NULL);
    ok(parent!=NULL,"parent\n");
    for(kind=0;kind<3;++kind)for(maximize=0;maximize<2;++maximize)for(visible=0;visible<2;++visible){
        HWND h;char sequence[512],*next=sequence;
        DWORD style=(kind==0?WS_OVERLAPPEDWINDOW:kind==1?WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_MAXIMIZEBOX:WS_CHILD|WS_CAPTION|WS_MINIMIZEBOX|WS_MAXIMIZEBOX);
        count=0;observed=NULL;recording=TRUE;
        h=CreateWindowA(cls.lpszClassName,"position",style|(maximize?WS_MAXIMIZE:WS_MINIMIZE)|(visible?WS_VISIBLE:0),40,50,200,150,kind==2?parent:NULL,NULL,cls.hInstance,NULL);
        recording=FALSE;ok(h!=NULL,"create\n");ok(count<=16,"complete position sequence\n");
        for(i=0;i<count && i<16;++i)next+=sprintf(next,"%x:%x,",messages[i],flags[i]);
        *next=0;
        trace("CREATE_POSITION kind=%u max=%u visible=%u count=%u sequence=%s\n",kind,maximize,visible,count,sequence);
        DestroyWindow(h);
    }
    DestroyWindow(parent);ok(SystemParametersInfoA(SPI_SETWORKAREA,0,&old_work,0),"restore work area\n");
}
