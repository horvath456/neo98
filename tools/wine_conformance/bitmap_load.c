/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
struct info { BITMAPINFOHEADER h; RGBQUAD colors[256]; };
static unsigned hash(const BYTE *p,unsigned n)
{
    unsigned h=2166136261u;
    while(n--)h=(h^*p++)*16777619u;
    return h;
}
START_TEST(bitmap_load)
{
    static const unsigned depths[]={1,4,8,16,24,32};
    static const unsigned flags[]={0,LR_MONOCHROME,LR_CREATEDIBSECTION,LR_CREATEDIBSECTION|LR_MONOCHROME,LR_VGACOLOR};
    static const unsigned sizes[][2]={{0,0},{7,5},{2,2},{1,1}};
    HDC dc=CreateCompatibleDC(NULL);
    unsigned d,f,s,x,y;
    ok(dc!=NULL,"DC\n");
    for(d=0;d<6;++d){
        struct info bi;
        BITMAPFILEHEADER file_header;
        void *bits=NULL;
        HBITMAP source,old;
        HANDLE file;
        DWORD written;
        unsigned colors=depths[d]<=8?1u<<depths[d]:0;
        unsigned pitch=((4*depths[d]+31)/32)*4;
        ZeroMemory(&bi,sizeof bi);
        bi.h.biSize=40;bi.h.biWidth=4;bi.h.biHeight=4;bi.h.biPlanes=1;bi.h.biBitCount=depths[d];
        for(x=0;x<colors;++x){bi.colors[x].rgbRed=(x&1)?255:0;bi.colors[x].rgbGreen=(x&2)?255:0;bi.colors[x].rgbBlue=(x&4)?255:0;}
        source=CreateDIBSection(dc,(BITMAPINFO*)&bi,0,&bits,NULL,0);
        ok(source!=NULL,"source depth=%u\n",depths[d]);if(!source)continue;
        ZeroMemory(bits,pitch*4);
        old=SelectObject(dc,source);
        for(y=0;y<4;++y)for(x=0;x<4;++x)SetPixel(dc,x,y,RGB((x&1)?255:0,(y&1)?255:0,(x+y)&1?255:0));
        SelectObject(dc,old);
        ZeroMemory(&file_header,sizeof file_header);
        file_header.bfType=0x4d42;file_header.bfOffBits=sizeof file_header+40+colors*4;
        file_header.bfSize=file_header.bfOffBits+pitch*4;
        file=CreateFileA("LOAD.BMP",GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);
        ok(file!=INVALID_HANDLE_VALUE,"file\n");if(file==INVALID_HANDLE_VALUE){DeleteObject(source);continue;}
        ok(WriteFile(file,&file_header,sizeof file_header,&written,NULL)&&written==sizeof file_header,"file header\n");
        ok(WriteFile(file,&bi,40+colors*4,&written,NULL)&&written==40+colors*4,"DIB header\n");
        ok(WriteFile(file,bits,pitch*4,&written,NULL)&&written==pitch*4,"DIB pixels\n");
        CloseHandle(file);
        for(f=0;f<5;++f)for(s=0;s<4;++s){
            HBITMAP loaded=(HBITMAP)LoadImageA(NULL,"LOAD.BMP",IMAGE_BITMAP,sizes[s][0],sizes[s][1],flags[f]|LR_LOADFROMFILE);
            BITMAP bm;
            struct info out;
            BYTE pixels[120];
            unsigned got;
            ok(loaded!=NULL,"load depth=%u flags=%x size=%u\n",depths[d],flags[f],s);
            if(!loaded)continue;
            ZeroMemory(&bm,sizeof bm);
            ok(GetObjectA(loaded,sizeof bm,&bm)==sizeof bm,"metadata\n");
            ZeroMemory(&out,sizeof out);out.h.biSize=40;out.h.biWidth=bm.bmWidth;out.h.biHeight=bm.bmHeight;out.h.biPlanes=1;out.h.biBitCount=24;
            ZeroMemory(pixels,sizeof pixels);
            got=GetDIBits(dc,loaded,0,bm.bmHeight,pixels,(BITMAPINFO*)&out,0);
            ok(got==bm.bmHeight,"pixels\n");
            trace("BITMAP_LOAD source=%u flags=%x size=%u width=%ld height=%ld depth=%u section=%u pixels=%08x\n",depths[d],flags[f],s,bm.bmWidth,bm.bmHeight,bm.bmBitsPixel,bm.bmBits!=NULL,hash(pixels,((bm.bmWidth*3+3)&~3)*bm.bmHeight));
            DeleteObject(loaded);
        }
        DeleteObject(source);DeleteFileA("LOAD.BMP");fflush(stdout);
    }
    DeleteDC(dc);
}
