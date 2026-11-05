/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static int CALLBACK record(HDC dc,HANDLETABLE *table,const ENHMETARECORD *r,int count,LPARAM data)
{
    trace("EMF_PATH record=%lu size=%lu\n",r->iType,r->nSize);
    ok(PlayEnhMetaFileRecord(dc,table,r,count),"play record %lu\n",r->iType);
    return 1;
}
START_TEST(emf_path)
{
    HDC dc=CreateEnhMetaFileA(NULL,NULL,NULL,NULL),target;HENHMETAFILE emf;RECT bounds={0,0,100,100};
    ok(dc!=NULL,"create emf\n");if(!dc)return;
    ok(BeginPath(dc),"begin\n");MoveToEx(dc,10,10,NULL);LineTo(dc,20,20);ok(CloseFigure(dc),"close\n");ok(EndPath(dc),"end\n");
    ok(BeginPath(dc),"begin second\n");ok(AbortPath(dc),"abort\n");
    emf=CloseEnhMetaFile(dc);ok(emf!=NULL,"close emf\n");if(!emf)return;
    target=CreateCompatibleDC(NULL);ok(target!=NULL,"target\n");
    ok(EnumEnhMetaFile(target,emf,record,NULL,&bounds),"enumerate and play\n");
    DeleteDC(target);DeleteEnhMetaFile(emf);
}
