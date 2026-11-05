// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U SHGetFileInfoA(const char*,U,void*,U,U);API U SHGetFileInfo(const char*,U,void*,U,U);API U SHGetFileInfoW(const void*,U,void*,U,U);
API U CreateFileA(const char*,U,U,U,U,U,U);API U CloseHandle(U);API U SetFileAttributesA(const char*,U);API U DeleteFileA(const char*);API U GetLastError(void);API void SetLastError(U);API void ExitProcess(U);
API U RegCreateKeyA(U,const char*,U*);API U RegSetValueExA(U,const char*,U,U,const void*,U);API U RegCloseKey(U);API U DestroyIcon(U);API U ImageList_GetImageCount(U);
struct Info{U icon,index,attributes;char display[260],type[80];};
static void check(int ok,U code){if(!ok)ExitProcess(code);}static int equal(const char*a,const char*b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
void mainCRTStartup(void){
 const char*path="C:\\FILEINFO.TST";U file=CreateFileA(path,0xc0000000,3,0,2,0,0);check(file!=~0u,42800);check(CloseHandle(file),42801);
 U key=0;check(!RegCreateKeyA(0x80000000,".tst",&key),42802);check(!RegSetValueExA(key,"",0,1,"Neo98.Test",11),42803);RegCloseKey(key);
 check(!RegCreateKeyA(0x80000000,"Neo98.Test",&key),42804);check(!RegSetValueExA(key,"",0,1,"Original file info test",sizeof("Original file info test")),42805);RegCloseKey(key);
 static struct Info info;check(SHGetFileInfoA(path,0,&info,sizeof info,0xe00),42806);check(info.attributes&0x40000000,42807);check(!(info.attributes&0x10000),42808);check(equal(info.display,"FILEINFO.TST")&&equal(info.type,"Original file info test"),42809);
 check(!RegCreateKeyA(0x80000000,"Neo98.Test",&key),42810);check(!RegSetValueExA(key,"IsShortcut",0,1,"",1),42811);RegCloseKey(key);info.attributes=0x10000;check(SHGetFileInfo(path,0,&info,sizeof info,0x20800)&&info.attributes==0x10000,42812);
 check(SetFileAttributesA(path,1),42813);info.attributes=~0u;check(SHGetFileInfoA(path,0,&info,sizeof info,0x800)&&(info.attributes&0x40000),42814);check(!(info.attributes&0x32),42815);
 check(SHGetFileInfoA("C:\\MISSING.DAT",16,&info,sizeof info,0xe10),42816);check(info.attributes&0x20000000,42817);check(equal(info.display,"MISSING.DAT"),42818);
 info.icon=123;info.display[0]='x';info.type[0]='x';check(!SHGetFileInfoA("C:\\MISSING.DAT",0,&info,sizeof info,0x800),42819);check(!info.icon&&!info.display[0]&&!info.type[0],42820);
 SetLastError(777);check(!SHGetFileInfoW((void*)1,~0u,(void*)1,0,~0u)&&GetLastError()==777,42821);
 // Plain file has no shortcut overlay. The returned icon owns a real image;
 // system list handle is the shared COMCTL list initialized from SHELL32 resources.
 check(SHGetFileInfoA("C:\\UNCREATED.BIN",0,&info,sizeof info,0x4111),42822);check(info.icon&&DestroyIcon(info.icon),42823);
 U list=SHGetFileInfoA("C:\\UNCREATED.BIN",0,&info,sizeof info,0x4011);check(list&&ImageList_GetImageCount(list)>=45&&info.index<ImageList_GetImageCount(list),42824);
 check(SetFileAttributesA(path,0)&&DeleteFileA(path),42825);ExitProcess(0);
}
