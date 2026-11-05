/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
START_TEST(window_words)
{
    WNDCLASSA wc; HWND handles[4]; unsigned h,i,op; static const int indices[]={-21,-16,-4,0,1,2,3,4,5};
    ZeroMemory(&wc,sizeof wc);wc.lpfnWndProc=DefWindowProcA;wc.hInstance=GetModuleHandleA(NULL);wc.lpszClassName="WordProbe";wc.cbWndExtra=5;
    ok(RegisterClassA(&wc)!=0,"register class\n");
    handles[0]=NULL;handles[1]=(HWND)0xdeadbeef;
    handles[2]=CreateWindowA(wc.lpszClassName,"",WS_POPUP,0,0,10,10,NULL,NULL,wc.hInstance,NULL);
    handles[3]=CreateWindowA(wc.lpszClassName,"",WS_POPUP,0,0,10,10,NULL,NULL,wc.hInstance,NULL);
    ok(handles[2] && handles[3],"create windows\n");DestroyWindow(handles[2]);
    for(h=0;h<4;++h)for(i=0;i<sizeof indices/sizeof *indices;++i)for(op=0;op<4;++op){
        DWORD result,error; int index=indices[i];
        if(index<0 && op>=2)continue;
        SetLastError(0xdeadbeef);
        if(op==0)result=GetWindowWord(handles[h],index);
        else if(op==1)result=GetWindowLongA(handles[h],index);
        else if(op==2)result=SetWindowWord(handles[h],index,0x1234);
        else result=SetWindowLongA(handles[h],index,0x12345678);
        error=GetLastError();
        trace("WINDOW_WORD handle=%u index=%d op=%u result=%08lx error=%08lx\n",h,index,op,result,error);
    }
    DestroyWindow(handles[3]);UnregisterClassA(wc.lpszClassName,wc.hInstance);
}
