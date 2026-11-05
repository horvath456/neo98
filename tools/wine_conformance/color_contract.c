/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"

START_TEST(color_contract)
{
    HDC dc=CreateDCA("DISPLAY",NULL,NULL,NULL), mem=CreateCompatibleDC(dc);
    HCOLORSPACE original, custom, previous;
    LOGCOLORSPACEA space;
    DWORD size, result, error; unsigned i;
    char path[MAX_PATH];
    ok(dc != NULL && mem != NULL,"DC creation failed\n");
    original=GetColorSpace(dc);
    trace("COLOR_DEFAULT present=%u type=%lu shared=%u\n",!!original,GetObjectType(original),GetColorSpace(mem)==original);
    memset(&space,0xcc,sizeof space);SetLastError(0xdeadbeef);
    result=GetLogColorSpaceA(original,&space,sizeof space);error=GetLastError();
    trace("COLOR_LOG result=%lu error=%lu size=%u\n",result,error,(unsigned)sizeof space);
    if(result) {
        const DWORD *words=(const DWORD *)&space;
        for(i=0;i<17;++i) trace("COLOR_WORD %u %08lx\n",i,words[i]);
        trace("COLOR_FILENAME %s\n",space.lcsFilename);
    }
    for(i=0;i<6;++i) {
        SetLastError(0xdeadbeef);result=SetICMMode(dc,i);error=GetLastError();
        trace("COLOR_MODE %u result=%lu error=%lu query=%d\n",i,result,error,SetICMMode(dc,ICM_QUERY));
    }
    SetICMMode(dc,ICM_OFF);SaveDC(dc);SetICMMode(dc,ICM_ON);RestoreDC(dc,-1);
    trace("COLOR_RESTORE mode=%d\n",SetICMMode(dc,ICM_QUERY));
    for(i=0;i<4;++i) {
        size=i==0?0:i==1?1:MAX_PATH;memset(path,0,sizeof path);
        SetLastError(0xdeadbeef);result=GetICMProfileA(dc,&size,i==3?NULL:path);error=GetLastError();
        trace("COLOR_PROFILE %u result=%lu error=%lu size=%lu path=%s\n",i,result,error,size,path);
    }
    memset(&space,0,sizeof space);space.lcsSignature=LCS_SIGNATURE;space.lcsVersion=0x400;
    space.lcsSize=sizeof space;space.lcsCSType=LCS_sRGB;space.lcsIntent=LCS_GM_IMAGES;
    SetLastError(0xdeadbeef);custom=CreateColorSpaceA(&space);error=GetLastError();
    trace("COLOR_CREATE present=%u error=%lu type=%lu\n",!!custom,error,GetObjectType(custom));
    if(custom) {
        SetLastError(0xdeadbeef);previous=SetColorSpace(dc,custom);error=GetLastError();
        trace("COLOR_SELECT previous=%u current=%u error=%lu\n",previous==original,GetColorSpace(dc)==custom,error);
        SetLastError(0xdeadbeef);result=DeleteColorSpace(custom);error=GetLastError();
        trace("COLOR_DELETE_SELECTED result=%lu error=%lu current=%u\n",result,error,GetColorSpace(dc)==custom);
        SetColorSpace(dc,original);DeleteColorSpace(custom);
    }
    DeleteDC(mem);DeleteDC(dc);
    fflush(stdout);
}
