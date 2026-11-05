/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static unsigned records;
static int CALLBACK record_proc(HDC dc,HANDLETABLE *table,METARECORD *record,int objects,LPARAM param)
{
    unsigned i;
    trace("ICON_RECORD index=%u function=%04x words=%lu\n",records++,record->rdFunction,record->rdSize);
        for(i=0;i<20 && i<record->rdSize-3;++i)trace("ICON_WORD index=%u value=%04x\n",i,record->rdParm[i]);
    return 1;
}
START_TEST(icon_metafile)
{
    HICON icon=LoadIconA(NULL,IDI_APPLICATION);HDC recorder=CreateMetaFileA(NULL),direct,play;
    HMETAFILE metafile;BITMAPINFO info;HBITMAP one,two;DWORD *a,*b;unsigned i,different=0,painted=0;
    ok(icon&&recorder,"icon and recording DC\n");
    ok(DrawIcon(recorder,0,0,icon),"record DrawIcon\n");
    metafile=CloseMetaFile(recorder);ok(metafile!=NULL,"close icon metafile\n");
    memset(&info,0,sizeof info);info.bmiHeader.biSize=sizeof info.bmiHeader;
    info.bmiHeader.biWidth=32;info.bmiHeader.biHeight=32;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    direct=CreateCompatibleDC(NULL);play=CreateCompatibleDC(NULL);
    one=CreateDIBSection(direct,&info,DIB_RGB_COLORS,(void **)&a,NULL,0);
    two=CreateDIBSection(play,&info,DIB_RGB_COLORS,(void **)&b,NULL,0);
    ok(one&&two,"icon surfaces\n");if(!one||!two)return;
    SelectObject(direct,one);SelectObject(play,two);
    for(i=0;i<1024;++i)a[i]=b[i]=0x00ffffff;
    DrawIcon(direct,0,0,icon);
    EnumMetaFile(play,metafile,record_proc,0);
    ok(PlayMetaFile(play,metafile),"play icon metafile\n");
    for(i=0;i<1024;++i){if((a[i]&0xffffff)!=(b[i]&0xffffff))++different;if((a[i]&0xffffff)!=0xffffff)++painted;}
    ok(painted!=0,"direct icon output is empty\n");
    ok(different==0,"icon metafile differs from direct output at %u pixels, records=%u\n",different,records);
    DeleteDC(direct);DeleteDC(play);DeleteObject(one);DeleteObject(two);DeleteMetaFile(metafile);
}
