/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
START_TEST(path_contract)
{
    HDC dc=CreateCompatibleDC(NULL); unsigned kind,width,wi,i,hash; int count,x,y;
    const unsigned widths[]={0,1,10};
    const POINT line[]={{20,20},{60,20},{60,60}};
    const DWORD styles[]={PS_SOLID,PS_GEOMETRIC|PS_ENDCAP_FLAT|PS_JOIN_MITER,
                         PS_GEOMETRIC|PS_ENDCAP_SQUARE|PS_JOIN_BEVEL};
    LOGBRUSH brush={BS_SOLID,0,0};
    ok(dc != NULL,"DC creation failed\n");
    for(kind=0;kind<6;++kind) for(wi=0;wi<3;++wi) {
        width=widths[wi];
        HPEN pen=kind<3?CreatePen(PS_SOLID,width,0):ExtCreatePen(styles[kind-3],width,&brush,0,NULL);
        HGDIOBJ old; HRGN region; RECT box; POINT points[256]; BYTE types[256];
        if(!pen) { trace("PATH_CREATE_FAILED %u %u\n",kind,width);continue; }
        old=SelectObject(dc,pen);BeginPath(dc);
        if(kind==0) Polyline(dc,line,2);
        else if(kind==1 || kind>=3) Polyline(dc,line,3);
        else { Polyline(dc,line,3);CloseFigure(dc); }
        EndPath(dc);SetLastError(0xdeadbeef);
        i=WidenPath(dc);x=GetLastError();count=GetPath(dc,NULL,NULL,0);
        trace("PATH_WIDEN %u %u result=%u error=%u count=%d\n",kind,width,i,x,count);
        if(i && count>0 && count<=256) {
            GetPath(dc,points,types,count);
            for(x=0;x<count;++x) trace("PATH_POINT %u %u %d %ld %ld %u\n",kind,width,x,points[x].x,points[x].y,types[x]);
        }
        region=PathToRegion(dc);hash=2166136261u;
        if(region) {
            GetRgnBox(region,&box);
            for(y=0;y<80;++y)for(x=0;x<80;++x)hash=(hash ^ !!PtInRegion(region,x,y))*16777619u;
            trace("PATH_REGION %u %u box=%ld,%ld,%ld,%ld hash=%08x center=%u edge=%u\n",kind,width,
                  box.left,box.top,box.right,box.bottom,hash,!!PtInRegion(region,40,20),!!PtInRegion(region,40,14));
            DeleteObject(region);
        }
        SelectObject(dc,old);DeleteObject(pen);fflush(stdout);
    }
    DeleteDC(dc);
}
