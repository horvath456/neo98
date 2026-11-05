/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
START_TEST(file_lookup)
{
    static const char *files[]={"plain","alpha.txt","long file.test"};
    static const char *queries[]={"plain","plain.","plain..","plain ","plain. ",
        "alpha.txt","alpha.txt.","alpha.txt..","alpha.txt ","alpha.txt. ",
        "long file.test.","long file.test...","long file.test   ",
        "missing","missing.",".","..","*.","*. ","*.txt.","*.txt ","*.*",
        "missing\\","missing\\.","missing\\..","plain\\","c:",
        "LOOKUP\\c:","bad:name","bad<name","bad>name","bad|name","bad\"name",
        "C:\\WTEST\\file_lookup\\LOOKUP\\c:"};
    unsigned i;CreateDirectoryA("LOOKUP",NULL);
    ok(SetCurrentDirectoryA("LOOKUP"),"enter fixture\n");
    for(i=0;i<3;++i){HANDLE h=CreateFileA(files[i],GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);ok(h!=INVALID_HANDLE_VALUE,"create %u\n",i);if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
    for(i=0;i<sizeof(queries)/sizeof(*queries);++i){
        WIN32_FIND_DATAA data;HANDLE h;DWORD attr,error,attrerror;char names[512]="";unsigned count=0;
        memset(&data,0,sizeof(data));SetLastError(0x1234);
        attr=GetFileAttributesA(queries[i]);attrerror=attr==INVALID_FILE_ATTRIBUTES?GetLastError():0;
        SetLastError(0x1234);h=FindFirstFileA(queries[i],&data);error=h==INVALID_HANDLE_VALUE?GetLastError():0;
        if(h!=INVALID_HANDLE_VALUE){do {if(strlen(names)+strlen(data.cFileName)+2>=sizeof(names))break;if(count)strcat(names,"|");strcat(names,data.cFileName);++count;}while(count<20&&FindNextFileA(h,&data));FindClose(h);}
        trace("FILE_LOOKUP case=%u attr=%08lx attrerror=%lu valid=%u error=%lu count=%u names=%s\n",i,attr,attrerror,h!=INVALID_HANDLE_VALUE,error,count,names);
    }
    for(i=0;i<3;++i)DeleteFileA(files[i]);SetCurrentDirectoryA("..");RemoveDirectoryA("LOOKUP");
}
