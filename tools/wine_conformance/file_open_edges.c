/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"

START_TEST(file_open_edges)
{
    static const char *names[]={NULL,"","\\","C:\\","C:\\NO98DIR\\X", "\\\\.\\CONFIGMG","\\\\.\\VWIN32"};
    unsigned i,disposition;
    for(i=0;i<sizeof names/sizeof names[0];++i){
        for(disposition=0;disposition<=6;++disposition){
            HANDLE handle;DWORD error;
            SetLastError(0xdeadbeef);
            handle=CreateFileA(names[i],GENERIC_READ|GENERIC_WRITE,3,NULL,disposition,0,NULL);
            error=GetLastError();
            trace("FILE_OPEN name=%u disposition=%u valid=%u error=%08lx\n",i,disposition,handle!=INVALID_HANDLE_VALUE,error);
            if(i>=5 && disposition==OPEN_EXISTING)
                ok(handle!=INVALID_HANDLE_VALUE,"existing VxD %u error=%lu\n",i,error);
            if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);
        }
    }
}
