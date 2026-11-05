/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include <string.h>
#include "wine/test.h"

START_TEST(strings)
{
    const unsigned length=0x1003a;
    char *source=HeapAlloc(GetProcessHeap(),0,length+1);
    char *target=HeapAlloc(GetProcessHeap(),0,length+2);
    char small[8];
    char *pages=VirtualAlloc(NULL,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old;
    WCHAR wide[4];
    char bytes[4];
    const char russian[]={ (char)0xe2,(char)0xa5,(char)0xe1,(char)0xe2 };
    ok(source && target && pages,"allocate string fixtures\n");
    if(!source || !target || !pages)goto done;
    memset(source,'x',length);source[length]=0;
    memset(target,'!',length+2);
    ok(lstrcpynA(target,source,length+1)==target,"long copy return\n");
    ok(!memcmp(target,source,length+1) && target[length+1]=='!',"copy beyond 64 KiB preserves bounds\n");
    memset(target,'!',length+2);
    ok(lstrcpyA(target,source)==target,"long unbounded copy return\n");
    ok(!memcmp(target,source,length+1) && target[length+1]=='!',"long unbounded copy contents\n");
    target[0]='a';target[1]=0;
    ok(lstrcatA(target,source)==target,"long append return\n");
    ok(target[0]=='a' && !memcmp(target+1,source,length+1),"long append contents\n");
    memset(small,'!',sizeof small);
    ok(lstrcpynA(small,source,4)==small && !memcmp(small,"xxx\0!",5),"bounded prefix\n");
    ok(lstrcpynA(small,source,0)==small && small[0]=='x',"zero capacity\n");
    trace("negative capacity return=%p\n",lstrcpynA(small,"",-1));
    ok(VirtualProtect(pages+4096,4096,PAGE_NOACCESS,&old),"guard page\n");
    memcpy(pages+4093,"abc",3);
    memset(small,'!',sizeof small);
    ok(lstrcpynA(small,pages+4093,4)==small && !memcmp(small,"abc\0!",5),"do not read beyond bounded source\n");
    ok(lstrcpynA(small,pages+4096,1)==small && small[0]==0,"capacity one needs no source byte\n");
    ok(IsValidCodePage(866),"original OEM Cyrillic code page available\n");
    ok(MultiByteToWideChar(866,0,russian,4,wide,4)==4 && wide[0]==0x442 && wide[1]==0x435 && wide[2]==0x441 && wide[3]==0x442,"CP866 decode\n");
    ok(WideCharToMultiByte(866,0,wide,4,bytes,4,NULL,NULL)==4 && !memcmp(bytes,russian,4),"CP866 roundtrip\n");
done:
    if(source)HeapFree(GetProcessHeap(),0,source);
    if(target)HeapFree(GetProcessHeap(),0,target);
    if(pages)VirtualFree(pages,0,MEM_RELEASE);
}
