/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static unsigned kind, step, notifications;
static LRESULT CALLBACK parent_proc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    if(message==WM_COMMAND&&HIWORD(wp)==CBN_SELCHANGE){
        char text[32]={0},list[32]={0};HWND combo=(HWND)lp;
        int selection=SendMessageA(combo,CB_GETCURSEL,0,0);
        GetWindowTextA(combo,text,sizeof(text));
        if(selection>=0)SendMessageA(combo,CB_GETLBTEXT,selection,(LPARAM)list);
        ++notifications;
        trace("COMBO_CONTRACT notify style=%u step=%u selection=%d text=[%s] list=[%s]\n",kind,step,selection,text,list);
    }
    return DefWindowProcA(window,message,wp,lp);
}
static void geometry(HWND combo,const char *stage)
{
    COMBOBOXINFO info={sizeof(info)};RECT client={0},drop={0},edit={0},list={0},listclient={0};
    BOOL found=GetComboBoxInfo(combo,&info);
    GetClientRect(combo,&client);SendMessageA(combo,CB_GETDROPPEDCONTROLRECT,0,(LPARAM)&drop);
    MapWindowPoints(NULL,combo,(POINT*)&drop,2);
    if(info.hwndItem&&info.hwndItem!=combo){GetWindowRect(info.hwndItem,&edit);MapWindowPoints(NULL,combo,(POINT*)&edit,2);}
    GetWindowRect(info.hwndList,&list);MapWindowPoints(NULL,combo,(POINT*)&list,2);GetClientRect(info.hwndList,&listclient);
    trace("COMBO_CONTRACT geometry style=%u stage=%s found=%u client=%ld,%ld itemheight=%ld drop=%ld,%ld,%ld,%ld item=%ld,%ld,%ld,%ld button=%ld,%ld,%ld,%ld edit=%ld,%ld,%ld,%ld list=%ld,%ld,%ld,%ld listclient=%ld,%ld listitem=%ld\n",
          kind,stage,!!found,client.right,client.bottom,SendMessageA(combo,CB_GETITEMHEIGHT,-1,0),
          drop.left,drop.top,drop.right,drop.bottom,info.rcItem.left,info.rcItem.top,info.rcItem.right,info.rcItem.bottom,
          info.rcButton.left,info.rcButton.top,info.rcButton.right,info.rcButton.bottom,edit.left,edit.top,edit.right,edit.bottom,
          list.left,list.top,list.right,list.bottom,listclient.right,listclient.bottom,SendMessageA(info.hwndList,LB_GETITEMHEIGHT,0,0));
}
START_TEST(combo_contract)
{
    WNDCLASSA cls={0};HWND parent;HFONT font;unsigned n;
    const unsigned keys[]={VK_DOWN,VK_DOWN,VK_UP,VK_UP,VK_END,VK_HOME};
    cls.lpfnWndProc=parent_proc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="ComboContract";
    ok(RegisterClassA(&cls)!=0,"register parent\n");
    parent=CreateWindowA(cls.lpszClassName,"",WS_OVERLAPPEDWINDOW,30,30,300,300,NULL,NULL,cls.hInstance,NULL);
    ok(parent!=NULL,"create parent\n");if(!parent)return;
    font=CreateFontA(10,0,0,0,0,0,0,0,SYMBOL_CHARSET,0,0,0,0,"Marlett");
    ok(font!=NULL,"create font\n");
    for(kind=1;kind<=3;++kind){
        HWND combo=CreateWindowA("COMBOBOX","",WS_CHILD|WS_VISIBLE|kind,5,5,100,100,parent,(HMENU)123,cls.hInstance,NULL);
        ok(combo!=NULL,"create style %u\n",kind);if(!combo)continue;
        geometry(combo,"initial");SendMessageA(combo,WM_SETFONT,(WPARAM)font,0);geometry(combo,"font10");
        SendMessageA(combo,CB_SETITEMHEIGHT,-1,12);geometry(combo,"height12");
        SendMessageA(combo,CB_SETITEMHEIGHT,-1,1);geometry(combo,"height1");
        SendMessageA(combo,WM_SETFONT,(WPARAM)GetStockObject(SYSTEM_FONT),0);geometry(combo,"system");
        SendMessageA(combo,CB_ADDSTRING,0,(LPARAM)"alpha");SendMessageA(combo,CB_ADDSTRING,0,(LPARAM)"beta");
        for(n=0;n<2;++n){
            COMBOBOXINFO info={sizeof(info)};HWND target=combo;
            if(n){GetComboBoxInfo(combo,&info);if(kind==3)continue;target=info.hwndItem;}
            SendMessageA(combo,CB_SETCURSEL,-1,0);
            for(step=n*6;step<(n+1)*6;++step){
                char text[32]={0};LRESULT result;notifications=0;
                result=SendMessageA(target,WM_KEYDOWN,keys[step%6],0);GetWindowTextA(combo,text,sizeof(text));
                trace("COMBO_CONTRACT key style=%u step=%u result=%ld notifications=%u selection=%ld text=[%s]\n",kind,step,result,notifications,SendMessageA(combo,CB_GETCURSEL,0,0),text);
            }
        }
        DestroyWindow(combo);
    }
    DeleteObject(font);DestroyWindow(parent);
}
