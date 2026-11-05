/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
START_TEST(frame_pixels)
{
    const unsigned cases[][2]={{1,0},{1,1},{1,2},{1,3},{1,4},
        {2,0},{2,1},{2,2},{2,4},{2,5},
        {3,0},{3,1},{3,2},{3,3},{3,4},{3,5},{3,8},{3,9},{3,10},
        {4,0},{4,1},{4,2},{4,4},{4,8},{4,16}};
    const unsigned flags[]={0,0x100,0x200,0x400,0x4000,0x8000};
    unsigned size,c,f,p;HDC dc=CreateCompatibleDC(NULL);BITMAPINFO info={0};DWORD *pixels;
    HBITMAP bitmap,old;HGDIOBJ font;COLORREF color=RGB(19,23,31);
    info.bmiHeader.biSize=40;info.bmiHeader.biWidth=48;info.bmiHeader.biHeight=-48;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,(void**)&pixels,NULL,0);
    ok(dc&&bitmap&&pixels,"create frame surface\n");if(!dc||!bitmap||!pixels)return;
    old=SelectObject(dc,bitmap);font=GetCurrentObject(dc,OBJ_FONT);SetTextColor(dc,color);
    for(size=1;size<=32;++size)for(c=0;c<sizeof cases/sizeof cases[0];++c)for(f=0;f<sizeof flags/sizeof flags[0];++f){
        RECT r={8,8,8+size,8+size};BOOL result;unsigned hash=2166136261u;
        PatBlt(dc,0,0,48,48,WHITENESS);
        result=DrawFrameControl(dc,&r,cases[c][0],cases[c][1]|flags[f]);GdiFlush();
        for(p=0;p<48*48;++p){unsigned rgb=pixels[p]&0xffffff;
            hash=(hash^(rgb&255))*16777619u;hash=(hash^((rgb>>8)&255))*16777619u;hash=(hash^(rgb>>16))*16777619u;}
        trace("FRAME size=%u type=%u state=%04x result=%u rect=%ld,%ld,%ld,%ld font=%u color=%08lx pixels=%08x\n",
            size,cases[c][0],cases[c][1]|flags[f],!!result,r.left,r.top,r.right,r.bottom,
            GetCurrentObject(dc,OBJ_FONT)==font,GetTextColor(dc),hash);
    }
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
}
