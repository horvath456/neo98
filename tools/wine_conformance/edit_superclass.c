/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include <string.h>
#include "wine/test.h"
static WNDPROC original;
static unsigned creates, destroys, selections;
static LRESULT CALLBACK edit_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp)
{
    if(msg==WM_NCCREATE)++creates;
    if(msg==WM_NCDESTROY)++destroys;
    if(msg==EM_SETSEL)++selections;
    return CallWindowProcA(original,hwnd,msg,wp,lp);
}
START_TEST(edit_superclass)
{
    WNDCLASSA cls={0};HWND parent,edit;unsigned variant;char text[64];LRESULT result;
    HINSTANCE instance=GetModuleHandleA(NULL);
    ok(GetClassInfoA(NULL,"EDIT",&cls),"read EDIT class\n");
    original=cls.lpfnWndProc;if(!original)return;
    cls.hInstance=instance;cls.lpszClassName="DirectEdit98";
    ok(RegisterClassA(&cls)!=0,"register direct superclass\n");
    cls.lpfnWndProc=edit_proc;cls.lpszClassName="WrappedEdit98";
    ok(RegisterClassA(&cls)!=0,"register wrapped superclass\n");
    parent=CreateWindowA("STATIC","parent",WS_OVERLAPPEDWINDOW,20,20,240,180,NULL,NULL,instance,NULL);
    ok(parent!=NULL,"create parent\n");if(!parent)return;
    for(variant=0;variant<4;++variant){
        const char *name=variant&1?"WrappedEdit98":"DirectEdit98";
        edit=CreateWindowA(name,"hello",WS_CHILD|WS_BORDER|(variant&2?ES_MULTILINE:0),
            0,0,180,80,parent,NULL,instance,NULL);
        ok(edit!=NULL,"create variant %u\n",variant);if(!edit)continue;
        text[0]=0;GetWindowTextA(edit,text,sizeof text);
        ok(!strcmp(text,"hello"),"initial text variant %u: %s\n",variant,text);
        result=SendMessageA(edit,EM_SETSEL,1,4);
        trace("EDIT_SUPER variant=%u setsel=%ld\n",variant,result);
        ok(SendMessageA(edit,EM_GETSEL,0,0)==MAKELONG(1,4),"selection variant %u\n",variant);
        SendMessageA(edit,EM_REPLACESEL,TRUE,(LPARAM)"i");
        GetWindowTextA(edit,text,sizeof text);
        ok(!strcmp(text,"hio"),"replace variant %u: %s\n",variant,text);
        ok(SendMessageA(edit,EM_CANUNDO,0,0)!=0,"undo available variant %u\n",variant);
        SendMessageA(edit,EM_UNDO,0,0);GetWindowTextA(edit,text,sizeof text);
        ok(!strcmp(text,"hello"),"undo variant %u: %s\n",variant,text);
        ok(DestroyWindow(edit),"destroy variant %u\n",variant);
    }
    ok(creates==2&&destroys==2,"wrapped lifetime %u/%u\n",creates,destroys);
    ok(selections==2,"EM_SETSEL delivered through subclass: %u\n",selections);
    DestroyWindow(parent);
    ok(UnregisterClassA("DirectEdit98",instance),"unregister direct class\n");
    ok(UnregisterClassA("WrappedEdit98",instance),"unregister wrapped class\n");
}
