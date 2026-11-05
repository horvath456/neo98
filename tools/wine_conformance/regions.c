/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* Native Win98 affine-region contracts and scan conversion observations. */
#include <windows.h>
#include "wine/test.h"

START_TEST(regions)
{
    struct { RGNDATAHEADER header; RECT rect[2]; } data;
    static const XFORM transforms[] = {
        {1,0,0,1,0,0}, {.5f,0,0,.5f,20,40},
        {1,0,0,1,-.5f,-.5f},
        {.3f,0,0,.3f,0,0}
    };
    unsigned i, hash; int x,y; HRGN r; RECT box;
    memset(&data,0,sizeof(data));
    data.header.dwSize=32; data.header.iType=RDH_RECTANGLES;
    data.header.nCount=2; data.header.nRgnSize=32;
    SetRect(&data.rect[0],0,0,8,4); SetRect(&data.rect[1],2,4,6,8);
    for(i=0;i<sizeof(transforms)/sizeof(transforms[0]);++i) {
        trace("REGION_BEGIN %u\n",i); fflush(stdout);
        r=ExtCreateRegion(&transforms[i],sizeof(data),(RGNDATA *)&data);
        ok(r != NULL,"transform %u failed: %lu\n",i,GetLastError());
        if(!r) continue;
        GetRgnBox(r,&box); hash=2166136261u;
        for(y=-16;y<64;++y) for(x=-16;x<64;++x)
            hash=(hash ^ !!PtInRegion(r,x,y))*16777619u;
        trace("REGION_TRACE %u %ld %ld %ld %ld %08x\n",i,box.left,box.top,box.right,box.bottom,hash);
        fflush(stdout); DeleteObject(r);
    }
}
