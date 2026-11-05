/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
START_TEST(font_choice)
{
    static const int sizes[]={-8,-10,-12,-14,-18,-20,-24,8,10,12,14,16,18,20,24,0};
    static const char *faces[]={"Symbol","Courier","MS Sans Serif","Arial"};
    HDC dc=CreateCompatibleDC(NULL);unsigned f,n;
    for(f=0;f<4;++f)for(n=0;n<sizeof sizes/sizeof sizes[0];++n){
        HFONT font=CreateFontA(sizes[n],0,0,0,400,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,0,faces[f]);
        HFONT old=SelectObject(dc,font);TEXTMETRICA tm;char name[64];
        ZeroMemory(&tm,sizeof tm);ZeroMemory(name,sizeof name);
        ok(font!=NULL,"font %s size=%d\n",faces[f],sizes[n]);
        ok(GetTextMetricsA(dc,&tm),"metrics %s size=%d\n",faces[f],sizes[n]);GetTextFaceA(dc,sizeof name,name);
        trace("FONT_CHOICE requested=%s size=%d selected=%s height=%ld ascent=%ld internal=%ld charset=%u type=%u\n",faces[f],sizes[n],name,tm.tmHeight,tm.tmAscent,tm.tmInternalLeading,tm.tmCharSet,tm.tmPitchAndFamily);
        SelectObject(dc,old);DeleteObject(font);
    }
    DeleteDC(dc);
}
