/* SPDX-License-Identifier: GPL-2.0-or-later */
/* VERSION.DLL must find raw-backed.rsrc with VirtualSize zero. */
#include <windows.h>
static void finish(DWORD code) {
 DWORD written; HANDLE f=CreateFileA("C:\\VSZERO.BIN",GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);
 WriteFile(f,&code,sizeof code,&written,0);CloseHandle(f);ExitProcess(code);
}
void mainCRTStartup(void) {
 char path[260]; BYTE data[4096]; DWORD handle=0, size=0; UINT bytes=0; VS_FIXEDFILEINFO *fixed=0;
 if(!GetModuleFileNameA(0,path,sizeof path))finish(10);
 size=GetFileVersionInfoSizeA(path,&handle);
 if(!size || size>sizeof data)finish(11);
 if(!GetFileVersionInfoA(path,handle,size,data))finish(12);
 if(!VerQueryValueA(data,"\\",(void**)&fixed,&bytes)||bytes!=sizeof *fixed)finish(13);
 if(fixed->dwSignature!=0xfeef04bd||fixed->dwFileVersionMS!=0x10002||fixed->dwFileVersionLS!=0x30004)finish(14);
 finish(0);
}
