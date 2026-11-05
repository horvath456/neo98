/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
struct info { BITMAPINFOHEADER h; RGBQUAD colors[256]; };
START_TEST(stretch_reduce)
{
    const unsigned sources[]={4,8,24},targets[]={1,8,32};
    HDC dc=CreateCompatibleDC(NULL);unsigned s,t,m,k,x;
    ok(dc!=NULL,"DC\n");
    for(s=0;s<3;++s)for(t=0;t<3;++t)for(m=1;m<=3;++m)for(k=0;k<2;++k){
        struct info input,output,query;
        BYTE bits[16]={0},pixels[12]={0};void *p;
        unsigned sw=k?5:2,dw=k?3:1,got;
        HBITMAP bitmap,old;
        ZeroMemory(&input,sizeof input);input.h.biSize=40;input.h.biWidth=sw;input.h.biHeight=1;input.h.biPlanes=1;input.h.biBitCount=sources[s];
        input.colors[0].rgbBlue=255;
        input.colors[1].rgbRed=input.colors[1].rgbBlue=255;
        input.colors[2].rgbGreen=255;
        for(x=0;x<sw;++x){unsigned v=x&1?2:1;
            if(sources[s]==4)bits[x/2]|=v<<((x&1)?0:4);
            else if(sources[s]==8)bits[x]=v;
            else{bits[x*3]=bits[x*3+2]=v==1?255:0;bits[x*3+1]=v==2?255:0;}
        }
        ZeroMemory(&output,sizeof output);output.h.biSize=40;output.h.biWidth=dw;output.h.biHeight=1;output.h.biPlanes=1;output.h.biBitCount=targets[t];
        if(targets[t]==1)output.colors[1].rgbRed=255;
        else{output.colors[0].rgbBlue=255;output.colors[1].rgbRed=output.colors[1].rgbBlue=255;output.colors[2].rgbGreen=255;output.colors[4].rgbRed=output.colors[4].rgbGreen=output.colors[4].rgbBlue=255;}
        bitmap=CreateDIBSection(dc,(BITMAPINFO*)&output,0,&p,NULL,0);ok(bitmap!=NULL,"target\n");if(!bitmap)continue;
        old=SelectObject(dc,bitmap);SetStretchBltMode(dc,m);
        got=StretchDIBits(dc,0,0,dw,1,0,0,sw,1,bits,(BITMAPINFO*)&input,0,SRCCOPY);
        ok(got==1,"stretch\n");SelectObject(dc,old);
        ZeroMemory(&query,sizeof query);query.h.biSize=40;query.h.biWidth=dw;query.h.biHeight=1;query.h.biPlanes=1;query.h.biBitCount=24;
        ok(GetDIBits(dc,bitmap,0,1,pixels,(BITMAPINFO*)&query,0)==1,"pixels\n");
        trace("STRETCH_REDUCE source=%u target=%u mode=%u width=%u pixels=%02x%02x%02x,%02x%02x%02x,%02x%02x%02x\n",sources[s],targets[t],m,sw,pixels[0],pixels[1],pixels[2],pixels[3],pixels[4],pixels[5],pixels[6],pixels[7],pixels[8]);
        DeleteObject(bitmap);fflush(stdout);
    }
    DeleteDC(dc);
}
