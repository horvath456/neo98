/* SPDX-License-Identifier: GPL-2.0-or-later */
/* bundled BWCC32 prefers 00010000, overlapping the system VM. */
#include <windows.h>
void mainCRTStartup(void)
{
    char text[512]; DWORD written;
    MEMORY_BASIC_INFORMATION before, after;
    VirtualQuery((void*)0x1c000,&before,sizeof before);
    HMODULE dll=LoadLibraryA("BWCC32.DLL");
    DWORD error=dll?0:GetLastError();
    VirtualQuery((void*)0x1c000,&after,sizeof after);
    int n=wsprintfA(text,"base=%08lx error=%lu low-before=%08lx low-after=%08lx\r\n",
                   (DWORD)dll,error,before.State,after.State);
    HANDLE f=CreateFileA("C:\\LOWIMAGE.TXT",GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);
    WriteFile(f,text,n,&written,0);CloseHandle(f);
    ExitProcess(!dll || (DWORD)dll < 0x100000 || before.State!=MEM_FREE || after.State!=MEM_FREE);
}
