/* SPDX-License-Identifier: GPL-2.0-or-later */
#define WINVER 0x0400
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include <string.h>
#include "wine/test.h"
START_TEST(oem_bitmaps)
{
    unsigned id,variant,x,y; HDC dc=CreateCompatibleDC(NULL);NONCLIENTMETRICSA saved={sizeof saved},profile;
    BOOL got=SystemParametersInfoA(SPI_GETNONCLIENTMETRICS,sizeof saved,&saved,0);
    ok(got,"read nonclient metrics\n");if(!got){DeleteDC(dc);return;}
    profile=saved;profile.iScrollWidth=profile.iScrollHeight=16;
    profile.iCaptionWidth=profile.iCaptionHeight=18;
    profile.iMenuWidth=profile.iMenuHeight=18;
    profile.iSmCaptionWidth=13;profile.iSmCaptionHeight=15;
    ok(SystemParametersInfoA(SPI_SETNONCLIENTMETRICS,sizeof profile,&profile,0),"use identical bitmap geometry\n");
    ok(dc!=NULL,"create bitmap DC\n");
    for(id=0;id<82;++id)trace("OEM_METRIC id=%u value=%d\n",id,GetSystemMetrics(id));
    for(id=0;id<29;++id)trace("OEM_COLOR id=%u value=%06lx\n",id,GetSysColor(id));
    for(id=0x7fde;id<=0x7fff;++id)for(variant=0;variant<3;++variant){
        BITMAP info={0}; HBITMAP bitmap,old;unsigned hash=2166136261u;
        SetLastError(0xdeadbeef);
        bitmap=variant?LoadImageA(NULL,MAKEINTRESOURCEA(id),IMAGE_BITMAP,
            variant==2?23:0,variant==2?19:0,variant==1?LR_MONOCHROME:0):
            LoadBitmapA(NULL,MAKEINTRESOURCEA(id));
        if(!bitmap){trace("OEM id=%04x variant=%u absent error=%lu\n",id,variant,GetLastError());continue;}
        ok(GetObjectA(bitmap,sizeof info,&info)==sizeof info,"bitmap info id=%04x variant=%u\n",id,variant);
        old=SelectObject(dc,bitmap);
        for(y=0;y<(unsigned)info.bmHeight;++y)for(x=0;x<(unsigned)info.bmWidth;++x){
            COLORREF pixel=GetPixel(dc,x,y);hash=(hash^pixel)*16777619u;
        }
        trace("OEM id=%04x variant=%u size=%ld,%ld stride=%ld planes=%u bpp=%u hash=%08x\n",id,variant,info.bmWidth,info.bmHeight,info.bmWidthBytes,info.bmPlanes,info.bmBitsPixel,hash);
        if(!variant){
            for(y=0;y<(unsigned)info.bmHeight;++y){char row[512];unsigned used=0;
                for(x=0;x<(unsigned)info.bmWidth&&used+7<sizeof row;++x)used+=sprintf(row+used,"%06lx ",GetPixel(dc,x,y));
                trace("OEM_PIXELS id=%04x y=%u %s\n",id,y,row);
            }
        }
        SelectObject(dc,old);ok(DeleteObject(bitmap),"delete bitmap id=%04x variant=%u\n",id,variant);
    }
    DeleteDC(dc);
    ok(SystemParametersInfoA(SPI_SETNONCLIENTMETRICS,sizeof saved,&saved,0),"restore nonclient metrics\n");
}
