/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
START_TEST(marlett_pixels)
{
    const char codes[]="012rs34568ahopbcdefgijklmn";
    const int sizes[]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
                       17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32};
    unsigned n,c,x,y;HDC dc=CreateCompatibleDC(NULL);
    BITMAPINFO info={0};DWORD *pixels;HBITMAP bitmap,oldbitmap;
    info.bmiHeader.biSize=40;info.bmiHeader.biWidth=32;info.bmiHeader.biHeight=-32;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,(void**)&pixels,NULL,0);
    ok(dc&&bitmap&&pixels,"create glyph surface\n");if(!dc||!bitmap||!pixels)return;
    oldbitmap=SelectObject(dc,bitmap);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(0,0,0));
    for(n=0;n<sizeof sizes/sizeof sizes[0];++n){
        TEXTMETRICA metrics;char face[64];
        HFONT font=CreateFontA(sizes[n],0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,SYMBOL_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,DEFAULT_PITCH,"Marlett");
        HFONT old=SelectObject(dc,font);
        ok(font!=NULL,"create Marlett size=%d\n",sizes[n]);
        GetTextFaceA(dc,sizeof face,face);GetTextMetricsA(dc,&metrics);
        trace("MARLETT_FONT size=%d face=%s height=%ld ascent=%ld descent=%ld\n",sizes[n],face,metrics.tmHeight,metrics.tmAscent,metrics.tmDescent);
        if(!n){
            DWORD bytes=GetFontData(dc,0,0,NULL,0);unsigned hash=2166136261u,i;
            unsigned char *data=bytes!=GDI_ERROR?HeapAlloc(GetProcessHeap(),0,bytes):NULL;
            ok(data&&GetFontData(dc,0,0,data,bytes)==bytes,"read selected font bytes\n");
            if(data){for(i=0;i<bytes;++i)hash=(hash^data[i])*16777619u;
                trace("MARLETT_FILE bytes=%lu fnv=%08x\n",bytes,hash);HeapFree(GetProcessHeap(),0,data);}
        }
        for(c=0;codes[c];++c){char row[320];unsigned used=0;
            PatBlt(dc,0,0,32,32,WHITENESS);
            ok(TextOutA(dc,0,0,codes+c,1),"draw size=%d code=%c\n",sizes[n],codes[c]);GdiFlush();
            for(y=0;y<32;++y){unsigned bits=0;
                for(x=0;x<32;++x)if((pixels[y*32+x]&0xffffff)!=0xffffff)bits|=1u<<x;
                used+=sprintf(row+used,"%08x ",bits);
            }
            trace("MARLETT_PIXELS size=%d code=%c rows=%s\n",sizes[n],codes[c],row);
        }
        SelectObject(dc,old);DeleteObject(font);
    }
    SelectObject(dc,oldbitmap);DeleteObject(bitmap);DeleteDC(dc);
}
