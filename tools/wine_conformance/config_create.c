/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
#include <string.h>
typedef DWORD (WINAPI *locate_fn)(DWORD*,char*,DWORD);
typedef DWORD (WINAPI *create_fn)(DWORD*,char*,DWORD,DWORD);
typedef DWORD (WINAPI *id_fn)(DWORD,char*,DWORD,DWORD);
typedef DWORD (WINAPI *parent_fn)(DWORD*,DWORD,DWORD);
START_TEST(config_create)
{
    HMODULE mod=LoadLibraryA("CFGMGR32.DLL");
    locate_fn locate=(locate_fn)GetProcAddress(mod,"CM_Locate_DevNodeA");
    create_fn create=(create_fn)GetProcAddress(mod,"CM_Create_DevNodeA");
    id_fn id=(id_fn)GetProcAddress(mod,"CM_Get_Device_IDA");
    parent_fn parent=(parent_fn)GetProcAddress(mod,"CM_Get_Parent");
    DWORD root=0,node=0,first=0,found=0,result,i;
    static const char *names[]={"ROOT\\NEO98PROBE\\0000","ROOT\\NEO98PROBE\\0000","NEO98PROBE","ROOT\\NEO98PROBE\\CHILD"};
    static const DWORD flags[]={2,2,6,2};
    {
        HANDLE device;DWORD arguments[6]={0},*pointer=arguments,output=0,bytes=0;BOOL success;
        SetLastError(0xdeadbeef);
        device=CreateFileA("\\\\.\\CONFIGMG",0,0,NULL,0,0,NULL);
        trace("CONFIG_DEVICE open=%u error=%08lx\n",device!=INVALID_HANDLE_VALUE,GetLastError());
        if(device!=INVALID_HANDLE_VALUE){
            SetLastError(0xdeadbeef);
            success=DeviceIoControl(device,0x80000000,&pointer,sizeof pointer,&output,sizeof output,&bytes,NULL);
            trace("CONFIG_DEVICE control=%u result=%lu bytes=%lu error=%08lx\n",success,output,bytes,GetLastError());
            SetLastError(0xdeadbeef);success=CloseHandle(device);
            trace("CONFIG_DEVICE close=%u error=%08lx\n",success,GetLastError());
        }
    }
    ok(locate&&create&&id&&parent,"configuration APIs\n");if(!locate||!create||!id||!parent)return;
    result=locate(&root,NULL,0);ok(result==0,"root result=%lu\n",result);
    for(i=0;i<4;++i){
        char name[200],actual[200];DWORD owner=0,previous=node;HKEY key=NULL;char path[240];LONG reg;
        strcpy(name,names[i]);node=0xdeadbeef;SetLastError(0xdeadbeef);
        result=create(&node,name,i==3?first:root,flags[i]);
        trace("CONFIG_CREATE i=%lu result=%lu output=%u zero=%u samefirst=%u error=%08lx input=%s\n",i,result,node!=0xdeadbeef,node==0,node==first,GetLastError(),name);
        ok(result==(i==1?16:i==2?4:0),"create %lu result=%lu\n",i,result);
        ok(i==2?node==0xdeadbeef:i==1?node==first:node!=0&&node!=0xdeadbeef,"create %lu output=%08lx\n",i,node);
        if(i==0 && !result)first=node;
        if(result)continue;
        ZeroMemory(actual,sizeof actual);result=id(node,actual,sizeof actual,0);
        ok(!result,"get ID %lu result=%lu\n",i,result);
        trace("CONFIG_ID i=%lu result=%lu name=%s\n",i,result,actual);
        result=parent(&owner,node,0);trace("CONFIG_PARENT i=%lu result=%lu matches=%u\n",i,result,owner==(i==3?first:root));
        ok(!result&&owner==(i==3?first:root),"parent %lu result=%lu\n",i,result);
        result=locate(&found,actual,0);trace("CONFIG_LOCATE i=%lu result=%lu matches=%u\n",i,result,found==node);
        ok(!result&&found==node,"locate %lu result=%lu\n",i,result);
        strcpy(path,"Enum\\");strcat(path,actual);reg=RegOpenKeyA(HKEY_LOCAL_MACHINE,path,&key);if(!reg)RegCloseKey(key);
        trace("CONFIG_REGISTRY i=%lu result=%ld\n",i,reg);
    }
    FreeLibrary(mod);
}
