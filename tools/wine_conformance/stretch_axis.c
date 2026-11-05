/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static unsigned hash(const BYTE *p,unsigned n){unsigned h=2166136261u;while(n--)h=(h^*p++)*16777619u;return h;}
START_TEST(stretch_axis)
{
    HDC dc=CreateCompatibleDC(NULL);unsigned sw,sh,dw,dh,mode,x,y;
    ok(dc!=NULL,"DC\n");
    for(sw=2;sw<=8;++sw)for(sh=1;sh<=4;++sh){
        BITMAPINFO input;BYTE bits[96]={0};unsigned pitch=((sw*3+3)&~3);
        ZeroMemory(&input,sizeof input);input.bmiHeader.biSize=40;input.bmiHeader.biWidth=sw;input.bmiHeader.biHeight=sh;input.bmiHeader.biPlanes=1;input.bmiHeader.biBitCount=24;
        for(y=0;y<sh;++y)for(x=0;x<sw;++x){bits[y*pitch+x*3]=(x*47+y*31+7)&255;bits[y*pitch+x*3+1]=(x*19+y*71+13)&255;bits[y*pitch+x*3+2]=(x*83+y*29+23)&255;}
        for(dw=1;dw<=9;++dw)for(dh=1;dh<=5;++dh)for(mode=1;mode<=3;++mode){
            BITMAPINFO output,query;BYTE pixels[140]={0};void *p;HBITMAP bitmap,old;unsigned rows;
            ZeroMemory(&output,sizeof output);output.bmiHeader.biSize=40;output.bmiHeader.biWidth=dw;output.bmiHeader.biHeight=dh;output.bmiHeader.biPlanes=1;output.bmiHeader.biBitCount=32;
            bitmap=CreateDIBSection(dc,&output,0,&p,NULL,0);ok(bitmap!=NULL,"target\n");if(!bitmap)continue;
            ZeroMemory(p,dw*dh*4);old=SelectObject(dc,bitmap);SetStretchBltMode(dc,mode);
            rows=StretchDIBits(dc,0,0,dw,dh,0,0,sw,sh,bits,&input,0,SRCCOPY);
            ok(rows==sh,"stretch\n");SelectObject(dc,old);
            query=output;query.bmiHeader.biBitCount=24;
            ok(GetDIBits(dc,bitmap,0,dh,pixels,&query,0)==dh,"pixels\n");
            trace("STRETCH_AXIS sw=%u sh=%u dw=%u dh=%u mode=%u pixels=%08x\n",sw,sh,dw,dh,mode,hash(pixels,((dw*3+3)&~3)*dh));
            DeleteObject(bitmap);
        }
        fflush(stdout);
    }
    DeleteDC(dc);
}
