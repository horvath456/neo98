/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static const char readonly_bytes[]="readonly image section";
static char writable_bytes[]="writable image section";
static DWORD inspect(const char *name,const char *stage,volatile BYTE *target,BOOL changed,DWORD previous,DWORD error)
{
    MEMORY_BASIC_INFORMATION info={0};DWORD size=VirtualQuery((void*)target,&info,sizeof(info));
    BOOL bad=IsBadWritePtr((void*)target,1),stored=FALSE;
    if(!bad){BYTE old=*target;*target=(BYTE)(old^0x5a);stored=*target==(BYTE)(old^0x5a);*target=old;}
    trace("IMAGE_ACCESS name=%s stage=%s query=%u protect=%08lx changed=%u previous=%08lx error=%lu bad=%u stored=%u\n",
          name,stage,size==sizeof(info),info.Protect,!!changed,previous,error,!!bad,!!stored);
    return info.Protect;
}
START_TEST(image_protection)
{
    HMODULE main=GetModuleHandleA(NULL);unsigned i;
    struct {const char *name;volatile BYTE *target;} regions[]={
        {"header",(BYTE*)main+16},{"readonly",(BYTE*)readonly_bytes},{"writable",(BYTE*)writable_bytes}};
    ok(main!=NULL,"main executable handle\n");
    for(i=0;i<3;++i){
        DWORD initial,previous,error;BOOL changed;
        initial=inspect(regions[i].name,"initial",regions[i].target,FALSE,0,0);
        previous=0xdeadbeef;SetLastError(0xdeadbeef);
        changed=VirtualProtect((void*)regions[i].target,1,PAGE_READONLY,&previous);error=changed?0:GetLastError();
        inspect(regions[i].name,"readonly",regions[i].target,changed,previous,error);
        previous=0xdeadbeef;SetLastError(0xdeadbeef);
        changed=VirtualProtect((void*)regions[i].target,1,PAGE_READWRITE,&previous);error=changed?0:GetLastError();
        inspect(regions[i].name,"readwrite",regions[i].target,changed,previous,error);
        previous=0xdeadbeef;SetLastError(0xdeadbeef);
        changed=VirtualProtect((void*)regions[i].target,1,initial,&previous);error=changed?0:GetLastError();
        inspect(regions[i].name,"restore",regions[i].target,changed,previous,error);
    }
}
