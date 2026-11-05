/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
struct info { BITMAPINFOHEADER h;RGBQUAD colors[256]; };
static unsigned hash(const BYTE *p,unsigned n){unsigned h=2166136261u;while(n--)h=(h^*p++)*16777619u;return h;}
START_TEST(bitmap_copy)
{
    static const unsigned depths[]={1,4,8,16,24,32};
    static const unsigned flags[]={0,LR_MONOCHROME,LR_CREATEDIBSECTION,LR_CREATEDIBSECTION|LR_MONOCHROME,LR_VGACOLOR};
    HDC dc=CreateCompatibleDC(NULL);unsigned d,f,resize,x,y;
    for(d=0;d<6;++d){
        struct info bi;void *bits=NULL;HBITMAP source,old;
        ZeroMemory(&bi,sizeof bi);bi.h.biSize=40;bi.h.biWidth=4;bi.h.biHeight=4;bi.h.biPlanes=1;bi.h.biBitCount=depths[d];
        if(depths[d]<=8)for(x=0;x<(1u<<depths[d]);++x){bi.colors[x].rgbRed=(x&1)?255:0;bi.colors[x].rgbGreen=(x&2)?255:0;bi.colors[x].rgbBlue=(x&4)?255:0;}
        source=CreateDIBSection(dc,(BITMAPINFO*)&bi,0,&bits,NULL,0);
        ok(source!=NULL,"source depth=%u\n",depths[d]);if(!source)continue;
        old=SelectObject(dc,source);
        for(y=0;y<4;++y)for(x=0;x<4;++x)SetPixel(dc,x,y,RGB((x&1)?255:0,(y&1)?255:0,(x+y)&1?255:0));
        SelectObject(dc,old);
        for(f=0;f<5;++f)for(resize=0;resize<2;++resize){
            HBITMAP copy=(HBITMAP)CopyImage(source,IMAGE_BITMAP,resize?7:0,resize?5:0,flags[f]);
            BITMAP bm;BYTE pixels[120];unsigned got=0;
            ok(copy!=NULL,"copy depth=%u flags=%x resize=%u\n",depths[d],flags[f],resize);if(!copy)continue;
            ZeroMemory(&bm,sizeof bm);ok(GetObjectA(copy,sizeof bm,&bm)==sizeof bm,"copy metadata\n");
            ZeroMemory(&bi,sizeof bi);bi.h.biSize=40;bi.h.biWidth=bm.bmWidth;bi.h.biHeight=bm.bmHeight;bi.h.biPlanes=1;bi.h.biBitCount=24;
            ZeroMemory(pixels,sizeof pixels);got=GetDIBits(dc,copy,0,bm.bmHeight,pixels,(BITMAPINFO*)&bi,0);
            ok(got==bm.bmHeight,"copy pixels depth=%u flags=%x rows=%u\n",depths[d],flags[f],got);
            trace("BITMAP_COPY source=%u flags=%x resize=%u width=%ld height=%ld depth=%u section=%u pixels=%08x\n",depths[d],flags[f],resize,bm.bmWidth,bm.bmHeight,bm.bmBitsPixel,bm.bmBits!=NULL,hash(pixels,((bm.bmWidth*3+3)&~3)*bm.bmHeight));
            {unsigned n;char bytes[241];static const char hex[]="0123456789abcdef";
             for(n=0;n<sizeof pixels;++n){bytes[n*2]=hex[pixels[n]>>4];bytes[n*2+1]=hex[pixels[n]&15];}bytes[240]=0;
             trace("BITMAP_BYTES source=%u flags=%x resize=%u data=%s\n",depths[d],flags[f],resize,bytes);}
            DeleteObject(copy);
        }
        DeleteObject(source);
    }
    DeleteDC(dc);
}
