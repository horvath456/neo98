/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static int CALLBACK replay(HDC dc,HANDLETABLE *table,const ENHMETARECORD *record,int count,LPARAM data)
{
    BOOL result; DWORD error;
    SetLastError(0xdeadbeef); result=PlayEnhMetaFileRecord(dc,table,record,count);error=GetLastError();
    trace("EMF_REPLAY type=%lu size=%lu result=%u error=%lu\n",record->iType,record->nSize,result,error);
    return 1;
}
static void mapped_text(void)
{
    BITMAPINFO info; HDC dc=CreateCompatibleDC(NULL); void *bits=NULL;
    HBITMAP bitmap,old_bitmap; HFONT font,old_font; RECT rect={5,5,60,30};
    POINT point; SIZE size; int dx[6]={5,4,3,2,1,5}; unsigned i,black=0,hash=2166136261u;
    memset(&info,0,sizeof(info));info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=160;info.bmiHeader.biHeight=-100;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,NULL,0);
    font=CreateFontA(7,0,0,0,0,0,0,0,0,0,0,0,0,"Arial");
    ok(dc&&bitmap&&bits&&font,"mapped text setup failed\n");
    if(!dc||!bitmap||!bits||!font)return;
    old_bitmap=SelectObject(dc,bitmap);old_font=SelectObject(dc,font);
    for(i=0;i<16000;++i)((DWORD*)bits)[i]=0x00ff00ff;
    SetMapMode(dc,MM_ANISOTROPIC);SetWindowExtEx(dc,10,10,NULL);SetViewportExtEx(dc,20,30,NULL);
    SetTextAlign(dc,TA_UPDATECP);MoveToEx(dc,10,10,NULL);SetTextColor(dc,0);SetBkColor(dc,0xffffff);
    ok(ExtTextOutA(dc,0,0,ETO_OPAQUE|ETO_CLIPPED,&rect,"Sample",6,dx),"mapped text failed\n");
    ok(GetCurrentPositionEx(dc,&point)&&point.x==30&&point.y==10,"current position %ld,%ld\n",point.x,point.y);
    ok(GetCurrentObject(dc,OBJ_FONT)==font,"drawing changed selected font\n");
    ok(GetViewportExtEx(dc,&size)&&size.cx==20&&size.cy==30,"drawing changed viewport\n");
    ok(((DWORD*)bits)[15*160+10]==0xffffff,"opaque rectangle was not mapped\n");
    ok(((DWORD*)bits)[0]==0x00ff00ff,"drawing escaped rectangle\n");
    for(i=0;i<16000;++i){unsigned pixel=((DWORD*)bits)[i]&0xffffff;black+=pixel==0;hash=(hash^pixel)*16777619u;}
    ok(black>0,"no text pixels drawn\n");
    trace("MAPPED_TEXT pixels=%u hash=%08x\n",black,hash);
    {
        TEXTMETRICA tm; unsigned width;
        BOOL measured=GetTextMetricsA(dc,&tm);
        trace("MAPPED_METRICS result=%u height=%ld average=%ld maximum=%ld\n",measured,tm.tmHeight,tm.tmAveCharWidth,tm.tmMaxCharWidth);
        SetMapMode(dc,MM_TEXT);SetTextAlign(dc,0);
        for(width=0;width<=10;++width) {
            HFONT physical=CreateFontA(21,width,0,0,0,0,0,0,0,0,0,0,0,"Arial");
            HFONT previous=SelectObject(dc,physical); RECT physical_rect={10,15,120,90};
            int physical_dx[6]={10,8,6,4,2,10};
            for(i=0;i<16000;++i)((DWORD*)bits)[i]=0x00ff00ff;
            ExtTextOutA(dc,20,30,ETO_OPAQUE|ETO_CLIPPED,&physical_rect,"Sample",6,physical_dx);
            black=0;hash=2166136261u;
            for(i=0;i<16000;++i){unsigned pixel=((DWORD*)bits)[i]&0xffffff;black+=pixel==0;hash=(hash^pixel)*16777619u;}
            GetTextMetricsA(dc,&tm);
            trace("PHYSICAL_TEXT width=%u average=%ld pixels=%u hash=%08x\n",width,tm.tmAveCharWidth,black,hash);
            SelectObject(dc,previous);DeleteObject(physical);
        }
    }
    SelectObject(dc,old_font);SelectObject(dc,old_bitmap);DeleteObject(font);DeleteObject(bitmap);DeleteDC(dc);
    ok(DeleteObject(GetStockObject(WHITE_BRUSH)),"stock brush deletion should report success\n");
    ok(GetObjectType(GetStockObject(WHITE_BRUSH))==OBJ_BRUSH,"stock brush was destroyed\n");
}
START_TEST(metafile_contract)
{
    HWND window=CreateWindowA("STATIC","EMF",WS_POPUP|WS_VISIBLE,0,0,200,200,0,0,0,0);
    HDC dc=GetDC(window),recording=CreateEnhMetaFileA(dc,NULL,NULL,NULL);
    HFONT font=CreateFontA(7,0,0,0,0,0,0,0,0,0,0,0,0,"Arial"),old;
    HENHMETAFILE file; RECT rect={0,0,100,100}; unsigned i; int dx[6]={5,4,3,2,1,5};
    ok(window&&dc&&recording&&font,"metafile setup failed\n");
    old=SelectObject(recording,font);
    ok(ExtTextOutA(recording,0,0,0,NULL,"Sample",6,NULL),"text without advances failed\n");
    ok(ExtTextOutA(recording,0,20,0,NULL,"Sample",6,dx),"text with advances failed\n");
    SelectObject(recording,old);ok(DeleteObject(font),"font deletion failed\n");
    file=CloseEnhMetaFile(recording);ok(file!=NULL,"close failed\n");
    SetLastError(0xdeadbeef);i=PlayEnhMetaFile(dc,file,&rect);
    trace("EMF_PLAY result=%u error=%lu\n",i,GetLastError());
    ok(i,"play failed\n");
    ok(EnumEnhMetaFile(dc,file,replay,0,&rect),"enumeration failed\n");
    DeleteEnhMetaFile(file);ReleaseDC(window,dc);DestroyWindow(window);mapped_text();fflush(stdout);
}
