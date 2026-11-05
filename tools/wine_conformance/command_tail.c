/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include <string.h>
#include "wine/test.h"

START_TEST(command_tail)
{
    STARTUPINFOA startup={0};PROCESS_INFORMATION process={0};
    char command[]="C:\\WINDOWS\\COMMAND.COM /c echo NEO98-COMMAND>C:\\CMD98.OUT";
    char buffer[64]={0};DWORD wait,code=259,read=0;HANDLE file;BOOL result;
    DeleteFileA("C:\\CMD98.OUT");startup.cb=sizeof startup;
    result=CreateProcessA(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&process);
    ok(result,"COMMAND.COM /c starts, error=%lu\n",GetLastError());
    if(!result)return;
    wait=WaitForSingleObject(process.hProcess,10000);
    ok(wait==WAIT_OBJECT_0,"COMMAND.COM /c terminates, wait=%lu\n",wait);
    if(wait!=WAIT_OBJECT_0)TerminateProcess(process.hProcess,99);
    result=GetExitCodeProcess(process.hProcess,&code);
    ok(result && !code,"COMMAND.COM exit=%lu error=%lu\n",code,GetLastError());
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
    file=CreateFileA("C:\\CMD98.OUT",GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    ok(file!=INVALID_HANDLE_VALUE,"redirected output exists, error=%lu\n",GetLastError());
    if(file!=INVALID_HANDLE_VALUE){
        result=ReadFile(file,buffer,sizeof buffer-1,&read,NULL);
        ok(result && read==15 && !memcmp(buffer,"NEO98-COMMAND\r\n",15),"output length=%lu text=%s\n",read,buffer);
        CloseHandle(file);
    }
    DeleteFileA("C:\\CMD98.OUT");
}
