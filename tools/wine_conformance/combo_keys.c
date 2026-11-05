/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static unsigned recording,used;
static char commands[256];
static LRESULT CALLBACK parent_proc(HWND h,UINT m,WPARAM w,LPARAM l)
{
    if(recording && m==WM_COMMAND && used+12<sizeof commands)
        used+=sprintf(commands+used,"%u,",HIWORD(w));
    if(recording && m>=WM_CTLCOLORMSGBOX && m<=WM_CTLCOLORSTATIC && used+12<sizeof commands)
        used+=sprintf(commands+used,"c%x,",m);
    return DefWindowProcA(h,m,w,l);
}
START_TEST(combo_keys)
{
    static const unsigned keys[]={VK_DOWN,VK_DOWN,VK_UP,VK_HOME,VK_END};
    WNDCLASSA cls={0};HWND parent,combo;unsigned kind,open,step;
    cls.lpfnWndProc=parent_proc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="ComboKeysParent";
    ok(RegisterClassA(&cls)!=0,"class\n");
    parent=CreateWindowA(cls.lpszClassName,"parent",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
        50,50,320,260,NULL,NULL,cls.hInstance,NULL);ok(parent!=NULL,"parent\n");
    for(kind=1;kind<=3;++kind)for(open=0;open<2;++open) {
        combo=CreateWindowA("COMBOBOX","",WS_CHILD|WS_VISIBLE|CBS_HASSTRINGS|kind,
            10,10,160,150,parent,(HMENU)10,cls.hInstance,NULL);ok(combo!=NULL,"combo %u/%u\n",kind,open);
        SendMessageA(combo,CB_ADDSTRING,0,(LPARAM)"alpha");
        SendMessageA(combo,CB_ADDSTRING,0,(LPARAM)"bravo");
        SendMessageA(combo,CB_ADDSTRING,0,(LPARAM)"charlie");
        SendMessageA(combo,CB_SETCURSEL,0,0);SetFocus(combo);UpdateWindow(combo);
        if(open)SendMessageA(combo,CB_SHOWDROPDOWN,TRUE,0);
        for(step=0;step<sizeof keys/sizeof keys[0];++step) {
            LRESULT result;used=0;commands[0]=0;recording=1;
            result=SendMessageA(combo,WM_KEYDOWN,keys[step],0);SendMessageA(combo,WM_KEYUP,keys[step],0);
            recording=0;
            trace("COMBO_KEYS kind=%u open=%u step=%u key=%u result=%ld selected=%ld dropped=%u commands=%s\n",
                kind,open,step,keys[step],result,SendMessageA(combo,CB_GETCURSEL,0,0),
                !!SendMessageA(combo,CB_GETDROPPEDSTATE,0,0),commands);
        }
        DestroyWindow(combo);
    }
    DestroyWindow(parent);
}
