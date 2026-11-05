/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
START_TEST(heap_interop)
{
    unsigned api,i; HANDLE heap=GetProcessHeap();
    for(api=0;api<2;++api){
        BYTE *p=HeapAlloc(heap,0,12),*grown; SIZE_T before;
        ok(p!=NULL,"HeapAlloc api=%u\n",api);if(!p)continue;
        for(i=0;i<12;++i)p[i]=(BYTE)(0x40+i);
        before=HeapSize(heap,0,p);
        ok((api?LocalSize(p):GlobalSize(p))==before,"heap pointer size api=%u\n",api);
        ok((api?LocalLock(p):GlobalLock(p))==p,"heap pointer lock api=%u\n",api);
        if(api)LocalUnlock(p);else GlobalUnlock(p);
        grown=api?LocalReAlloc(p,8192,LMEM_MOVEABLE|LMEM_ZEROINIT):GlobalReAlloc(p,8192,GMEM_MOVEABLE|GMEM_ZEROINIT);
        ok(grown!=NULL,"heap pointer realloc api=%u\n",api);
        if(!grown){HeapFree(heap,0,p);continue;}
        for(i=0;i<12&&grown[i]==0x40+i;++i){}
        ok(i==12,"realloc retains payload api=%u byte=%u\n",api,i);
        for(i=(unsigned)before;i<8192&&!grown[i];++i){}
        ok(i==8192,"realloc zeroes extension api=%u byte=%u\n",api,i);
        ok(HeapSize(heap,0,grown)>=8192,"realloc stays on process heap api=%u\n",api);
        ok((api?LocalSize(grown):GlobalSize(grown))==HeapSize(heap,0,grown),"size after realloc api=%u\n",api);
        ok((api?LocalFree(grown):GlobalFree(grown))==NULL,"heap pointer free api=%u\n",api);
    }
}
