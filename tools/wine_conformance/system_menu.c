/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static void inspect(HWND h,const char *step)
{
    HMENU menu=GetSystemMenu(h,FALSE);int i,count=GetMenuItemCount(menu);
    unsigned close=GetMenuState(menu,SC_CLOSE,MF_BYCOMMAND),separators=0;
    for(i=0;i<count;++i)if(GetMenuState(menu,i,MF_BYPOSITION)&MF_SEPARATOR)++separators;
    trace("SYSTEM_MENU step=%s exists=%u count=%d close=%lx separators=%u style=%lx\n",step,menu!=NULL,count,(unsigned long)close,separators,(unsigned long)GetClassLongA(h,GCL_STYLE));
}
START_TEST(system_menu)
{
    WNDCLASSA cls;HWND h;unsigned mode;
    memset(&cls,0,sizeof cls);cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="SystemMenuContract";
    for(mode=0;mode<2;++mode){
        cls.style=mode?CS_NOCLOSE:0;ok(RegisterClassA(&cls)!=0,"register %u\n",mode);
        h=CreateWindowA(cls.lpszClassName,"menu",WS_OVERLAPPEDWINDOW,0,0,100,100,NULL,NULL,cls.hInstance,NULL);ok(h!=NULL,"window %u\n",mode);
        inspect(h,mode?"noclose":"normal");
        GetSystemMenu(h,TRUE);inspect(h,mode?"noclose-reset-unchanged":"normal-reset-unchanged");
        SetClassLongA(h,GCL_STYLE,mode?0:CS_NOCLOSE);
        inspect(h,mode?"noclose-changed":"normal-changed");
        GetSystemMenu(h,TRUE);inspect(h,mode?"noclose-reset":"normal-reset");
        DestroyWindow(h);UnregisterClassA(cls.lpszClassName,cls.hInstance);
    }
}
