/* SPDX-License-Identifier: GPL-2.0-or-later
 * Focused control contracts and geometry traces for the Win98 comparison. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "wine/test.h"

static void edit_geometry(void)
{
    static const char text[]="this is a long string.";
    HWND parent=CreateWindowA("STATIC","",0,0,0,10,10,NULL,NULL,NULL,NULL);
    HWND edit=CreateWindowA("EDIT","",ES_MULTILINE,10,10,50,50,parent,NULL,NULL,NULL);
    HDC dc=GetDC(edit);
    HFONT font=(HFONT)SendMessageA(edit,WM_GETFONT,0,0), old=NULL;
    TEXTMETRICA tm;
    RECT client, format;
    SIZE size;
    if(font)old=SelectObject(dc,font);
    GetTextMetricsA(dc,&tm);GetTextExtentPoint32A(dc,text,strlen(text),&size);
    GetClientRect(edit,&client);SendMessageA(edit,EM_GETRECT,0,(LPARAM)&format);
    trace("default EDIT style=%08lx font=%p metrics=%ld/%ld text=%ld/%ld client=%ld,%ld,%ld,%ld format=%ld,%ld,%ld,%ld\n",
        GetWindowLongA(edit,GWL_STYLE),font,tm.tmHeight,tm.tmAveCharWidth,size.cx,size.cy,
        client.left,client.top,client.right,client.bottom,format.left,format.top,format.right,format.bottom);
    if(old)SelectObject(dc,old);ReleaseDC(edit,dc);
    SendMessageA(edit,EM_REPLACESEL,0,(LPARAM)text);
    ok(SendMessageA(edit,WM_GETTEXTLENGTH,0,0)==0,"fixed-height multiline insertion must fit completely\n");
    SendMessageA(edit,WM_SETTEXT,0,(LPARAM)"abc\r\ndef");
    SendMessageA(edit,EM_SETSEL,1,2);
    {
        DWORD start=0,end=0;
        LRESULT point=SendMessageA(edit,EM_POSFROMCHAR,5,0);
        LRESULT index=SendMessageA(edit,EM_CHARFROMPOS,0,point);
        SendMessageA(edit,EM_GETSEL,(WPARAM)&start,(LPARAM)&end);
        ok(LOWORD(index)==5 && HIWORD(index)==1,"position roundtrip across CRLF: %lx\n",index);
        ok(start==1 && end==2,"position query must preserve selection\n");
    }
    DestroyWindow(edit);DestroyWindow(parent);
}

static void font_margins(void)
{
    HWND edit=CreateWindowA("EDIT","",WS_POPUP,0,0,1000,100,NULL,NULL,NULL,NULL);
    HFONT font=CreateFontA(16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,0,"Arial");
    int width;
    {
        HDC dc=GetDC(edit);HFONT old=SelectObject(dc,font);TEXTMETRICA tm;ABC abc[256];
        int left=0,right=0,i;
        GetTextMetricsA(dc,&tm);
        if(GetCharABCWidthsA(dc,0,255,abc))for(i=0;i<256;++i){if(-abc[i].abcA>left)left=-abc[i].abcA;if(-abc[i].abcC>right)right=-abc[i].abcC;}
        trace("Arial16 height=%ld average=%ld maximum=%ld ABC-overhang=%d/%d\n",tm.tmHeight,tm.tmAveCharWidth,tm.tmMaxCharWidth,left,right);
        SelectObject(dc,old);ReleaseDC(edit,dc);
    }
    for(width=8;width<=80;width+=2){
        DWORD margins;
        SetWindowPos(edit,NULL,0,0,width,100,SWP_NOZORDER|SWP_NOACTIVATE);
        SendMessageA(edit,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELONG(1,1));
        SendMessageA(edit,WM_SETFONT,(WPARAM)font,0);
        margins=SendMessageA(edit,EM_GETMARGINS,0,0);
        trace("Arial16 width=%d margins=%u/%u\n",width,LOWORD(margins),HIWORD(margins));
    }
    SendMessageA(edit,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELONG(12,7));
    ok(SendMessageA(edit,EM_GETMARGINS,0,0)==MAKELONG(12,7),"independent margins\n");
    DestroyWindow(edit);DeleteObject(font);
}
START_TEST(controls)
{
    edit_geometry();
    font_margins();
}
