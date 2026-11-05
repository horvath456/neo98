/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* measure the Win98 replacement primitive without changing either file. */
#include <windows.h>
#include <stdio.h>
static int put(const char *name, const char *text)
{
    FILE *f=fopen(name,"wb"); if(!f)return 0;
    fputs(text,f);return fclose(f)==0;
}
int main(void)
{
    typedef BOOL (WINAPI *MoveEx)(LPCSTR,LPCSTR,DWORD);
    MoveEx move=(MoveEx)GetProcAddress(GetModuleHandleA("KERNEL32.DLL"),"MoveFileExA");
    const char *from="C:\\REN4FROM.TXT",*to="C:\\REN4TO.TXT";
    if(!put(from,"source")||!put(to,"target"))return 2;
    SetLastError(0);BOOL ok=MoveFileA(from,to);DWORD error=GetLastError();
    printf("MoveFileA existing: ok=%u error=%lu\n",ok,(unsigned long)error);
    if(!move)return 3;
    SetLastError(0);ok=move(from,to,1);error=GetLastError();
    printf("MoveFileExA REPLACE_EXISTING: ok=%u error=%lu\n",ok,(unsigned long)error);
    char a[16]={0},b[16]={0};FILE *f=fopen(from,"rb");if(f){fread(a,1,15,f);fclose(f);}
    f=fopen(to,"rb");if(f){fread(b,1,15,f);fclose(f);}
    printf("contents: %s / %s\n",a,b);DeleteFileA(from);DeleteFileA(to);
    return 0;
}
