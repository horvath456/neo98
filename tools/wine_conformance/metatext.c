/* SPDX-License-Identifier: GPL-2.0-or-later
 * WordPad clipboard recording: Unicode text into a classic metafile. */
#include <windows.h>
#include <string.h>
#include "wine/test.h"

static unsigned word(const BYTE *p) { return p[0] | ((unsigned)p[1]<<8); }
static DWORD dword(const BYTE *p) { return word(p) | ((DWORD)word(p+2)<<16); }

START_TEST(metatext)
{
    HDC dc=CreateMetaFileA(NULL), playback;
    HMETAFILE meta;
    RECT rect={1,2,100,40};
    WCHAR text[]={'A',0xe9,'Z'}, longer[1500];
    INT dx[]={9,11,13}, advances[1500];
    BYTE bits[16000];
    UINT size,at,short_run=0,long_run=0;
    unsigned i;
    ok(dc!=NULL,"create recording DC\n");
    if(!dc)return;
    ok(ExtTextOutW(dc,3,4,ETO_OPAQUE|ETO_CLIPPED,&rect,text,3,dx),"record Unicode text and advances\n");
    ok(TextOutW(dc,5,6,text,3),"record simple Unicode text\n");
    for(i=0;i<1500;++i){longer[i]='x';advances[i]=1;}
    ok(ExtTextOutW(dc,0,0,0,NULL,longer,1500,advances),"record long text with advances\n");
    meta=CloseMetaFile(dc);
    ok(meta!=NULL,"close recording\n");
    if(!meta)return;
    size=GetMetaFileBitsEx(meta,0,NULL);
    ok(size>18 && size<=sizeof bits,"bounded metafile size %u\n",size);
    if(size>18 && size<=sizeof bits){
        ok(GetMetaFileBitsEx(meta,size,bits)==size,"read records\n");
        for(at=18;at+6<=size;){
            DWORD words=dword(bits+at);
            unsigned fn=word(bits+at+4);
            ok(words>=3 && words<=(size-at)/2,"bounded record\n");
            if(words<3 || words>(size-at)/2)break;
            if(fn==0x0a32 && words>=7){
                const BYTE *p=bits+at+6;
                unsigned count=word(p+4),flags=word(p+6),offset=8+((flags&6)?8:0);
                if(count==3 && flags==6 && words*2>=6+offset+10){
                    ++short_run;
                    ok(word(p)==4 && word(p+2)==3,"record coordinates\n");
                    ok(word(p+8)==1 && word(p+10)==2 && word(p+12)==100 && word(p+14)==40,"record rectangle\n");
                    ok(p[offset]=='A' && p[offset+1]==0xe9 && p[offset+2]=='Z',"Win98 ANSI text conversion\n");
                    ok(word(p+offset+4)==9 && word(p+offset+6)==11 && word(p+offset+8)==13,"record character advances\n");
                }
                if(count==1500 && words*2>=6+offset+4500){
                    ++long_run;
                    ok(p[offset]=='x' && p[offset+1499]=='x',"long text retained\n");
                    ok(word(p+offset+1500)==1 && word(p+offset+4498)==1,"long advance array retained\n");
                }
            }
            at+=words*2;
        }
        ok(short_run==1 && long_run==1,"both extended runs recorded\n");
    }
    playback=CreateCompatibleDC(NULL);
    ok(playback!=NULL,"create playback DC\n");
    if(playback){ok(PlayMetaFile(playback,meta),"play recorded text\n");DeleteDC(playback);}
    ok(DeleteMetaFile(meta),"delete recorded text\n");
}
