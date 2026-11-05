/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include <string.h>
#include "wine/test.h"
static unsigned command;
static LRESULT CALLBACK owner_proc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp)
{
    if(message==WM_COMMAND){command=wp;return 0;}
    return DefWindowProcA(hwnd,message,wp,lp);
}
START_TEST(accelerator_modifiers)
{
    WNDCLASSA cls={0};HWND owner;ACCEL entries[8];HACCEL table;MSG msg={0};
    BYTE saved[256],state[256];unsigned mask,system;int result;
    cls.lpfnWndProc=owner_proc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="AccelMods98";
    ok(RegisterClassA(&cls)!=0,"register owner\n");
    owner=CreateWindowA(cls.lpszClassName,"accelerators",WS_OVERLAPPEDWINDOW|WS_VISIBLE,30,30,220,160,NULL,NULL,cls.hInstance,NULL);
    ok(owner!=NULL,"create owner\n");if(!owner)return;
    for(mask=0;mask<8;++mask){entries[mask].fVirt=FVIRTKEY|((mask&1)?FSHIFT:0)|((mask&2)?FCONTROL:0)|((mask&4)?FALT:0);entries[mask].key='N';entries[mask].cmd=100+mask;}
    table=CreateAcceleratorTableA(entries,8);ok(table!=NULL,"create table\n");
    GetKeyboardState(saved);
    for(system=0;system<2;++system)for(mask=0;mask<8;++mask){
        memset(state,0,sizeof state);state[VK_SHIFT]=(mask&1)?0x80:0;state[VK_CONTROL]=(mask&2)?0x80:0;state[VK_MENU]=(mask&4)?0x80:0;
        SetKeyboardState(state);command=0;
        msg.hwnd=owner;msg.message=system?WM_SYSKEYDOWN:WM_KEYDOWN;msg.wParam='N';msg.lParam=(mask&4)?0x20310001:0x00310001;
        result=TranslateAcceleratorA(owner,table,&msg);
        trace("ACCEL_MOD system=%u mask=%u result=%u command=%u\n",system,mask,result,command);
        ok(result==1&&command==(0x10000|100+mask),"system=%u mask=%u result=%u command=%u\n",system,mask,result,command);
    }
    SetKeyboardState(saved);ok(DestroyAcceleratorTable(table),"destroy created table\n");
    DestroyWindow(owner);UnregisterClassA(cls.lpszClassName,cls.hInstance);
}
