/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
struct info { BITMAPINFOHEADER h; RGBQUAD colors[256]; };
static unsigned hash(const BYTE *p,unsigned n){unsigned h=2166136261u;while(n--)h=(h^*p++)*16777619u;return h;}
START_TEST(stretch_modes)
{
    const unsigned depths[]={1,4,8,16,24,32};
    const unsigned sizes[]={1,2,7};
    HDC dc=CreateCompatibleDC(NULL);
    unsigned source_depth,target_depth,mode,size,x,y;
    ok(dc!=NULL,"DC\n");
    for(source_depth=0;source_depth<6;++source_depth){
        struct info source_info;
        HBITMAP source,old;
        void *source_bits;
        unsigned pitch=((4*depths[source_depth]+31)/32)*4;
        ZeroMemory(&source_info,sizeof source_info);
        source_info.h.biSize=40;source_info.h.biWidth=4;source_info.h.biHeight=4;source_info.h.biPlanes=1;source_info.h.biBitCount=depths[source_depth];
        if(depths[source_depth]<=8)for(x=0;x<(1u<<depths[source_depth]);++x){source_info.colors[x].rgbRed=x&1?255:0;source_info.colors[x].rgbGreen=x&2?255:0;source_info.colors[x].rgbBlue=x&4?255:0;}
        source=CreateDIBSection(dc,(BITMAPINFO*)&source_info,0,&source_bits,NULL,0);
        ok(source!=NULL,"source\n");if(!source)continue;
        ZeroMemory(source_bits,pitch*4);old=SelectObject(dc,source);
        for(y=0;y<4;++y)for(x=0;x<4;++x)SetPixel(dc,x,y,RGB(x&1?255:0,y&1?255:0,(x+y)&1?255:0));
        SelectObject(dc,old);
        for(target_depth=0;target_depth<7;++target_depth)for(mode=1;mode<=3;++mode)for(size=0;size<3;++size){
            struct info target_info,out;
            HBITMAP target;
            void *target_bits=NULL;
            BYTE pixels[168];
            unsigned width=sizes[size],height=sizes[size],got,drawn;
            ZeroMemory(&target_info,sizeof target_info);target_info.h.biSize=40;target_info.h.biWidth=width;target_info.h.biHeight=height;target_info.h.biPlanes=1;target_info.h.biBitCount=target_depth==6?1:depths[target_depth];
            if(target_depth<3)for(x=0;x<(1u<<depths[target_depth]);++x){target_info.colors[x].rgbRed=x&1?255:0;target_info.colors[x].rgbGreen=x&2?255:0;target_info.colors[x].rgbBlue=x&4?255:0;}
            target=target_depth==6?CreateBitmap(width,height,1,1,NULL):CreateDIBSection(dc,(BITMAPINFO*)&target_info,0,&target_bits,NULL,0);
            ok(target!=NULL,"target\n");if(!target)continue;
            old=SelectObject(dc,target);SetStretchBltMode(dc,mode);
            got=StretchDIBits(dc,0,0,width,height,0,0,4,4,source_bits,(BITMAPINFO*)&source_info,0,SRCCOPY);
            drawn=got;ok(got!=0&&got!=GDI_ERROR,"stretch rows=%u\n",got);SelectObject(dc,old);
            ZeroMemory(&out,sizeof out);out.h.biSize=40;out.h.biWidth=width;out.h.biHeight=height;out.h.biPlanes=1;out.h.biBitCount=24;
            ZeroMemory(pixels,sizeof pixels);got=GetDIBits(dc,target,0,height,pixels,(BITMAPINFO*)&out,0);
            ok(got==height,"pixels\n");
            trace("STRETCH_MODE source=%u target=%u section=%u mode=%u size=%u rows=%u pixels=%08x\n",depths[source_depth],target_info.h.biBitCount,target_depth!=6,mode,width,drawn,hash(pixels,((width*3+3)&~3)*height));
            DeleteObject(target);
        }
        DeleteObject(source);fflush(stdout);
    }
    DeleteDC(dc);
}
