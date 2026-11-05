/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../build/shell-icons/fixture-images.h"
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U ExtractIconA(U,const char *,U);
API U ExtractIconExA(const char *,U,U *,U *,U);
API U ExtractIconEx(const char *,U,U *,U *,U);
API U ExtractIconW(U,const void *,U);
API U InternalExtractIconListA(U,U,U);
API U InternalExtractIconListW(U,U,U);
API U ExtractIconExW(const void *,U,U *,U *,U);
API U ExtractAssociatedIconExW(U,const void *,void *,void *);
API U DestroyIcon(U);
API U DrawIconEx(U,int,int,U,int,int,U,U,U);
API U CreateFileA(const char *,U,U,U,U,U,U);
API U WriteFile(U,const void *,U,U *,U);
API U CloseHandle(U);
API U DeleteFileA(const char *);
API U SetEnvironmentVariableA(const char *,const char *);
API U CreateCompatibleDC(U);
API U CreateBitmap(int,int,U,U,const void *);
API U SelectObject(U,U);
API U DeleteDC(U);
API U DeleteObject(U);
API U GetPixel(U,int,int);
API void SetLastError(U);
API U GetLastError(void);
static void check(int good,U error) { if(!good)ExitProcess(error); }
static void write_file(const char *name,const void *bytes,U size) {
    U file=CreateFileA(name,0x40000000,1,0,2,0,0),done=0;
    check(file!=~0u,8500);check(WriteFile(file,bytes,size,&done,0) && done==size,8501);check(CloseHandle(file),8502);
}
static void draw(U dc,U icon,U color) {
    check(DrawIconEx(dc,0,0,icon,32,32,0,0,3),8503);
    check(GetPixel(dc,0,0)==color && GetPixel(dc,31,0)==(color^0x00ff00) &&
          GetPixel(dc,0,31)==(color^0x332211) && GetPixel(dc,31,31)==(color^0xff0000),8504);
}
void run_shell_icons_test(void) {
    SetLastError(0x9812);
    check(!ExtractIconW(~0u,(void *)~0u,~0u) && GetLastError()==0x9812,8532);
    check(!InternalExtractIconListA(~0u,~0u,~0u) && !InternalExtractIconListW(~0u,~0u,~0u) && GetLastError()==0x9812,8533);
    check(!ExtractIconExW((void *)~0u,~0u,(U *)~0u,(U *)~0u,~0u) && GetLastError()==120,8534);
    SetLastError(0x9812);
    check(!ExtractAssociatedIconExW(~0u,(void *)~0u,(void *)~0u,(void *)~0u) && GetLastError()==120,8535);
    const char *path="C:\\NEO98-SHELL-ICONS.BIN";
    static const U zeros[32*32]={0};
    U dc=CreateCompatibleDC(0),bitmap=CreateBitmap(32,32,1,32,zeros);
    check(dc && bitmap,8505);U previous=SelectObject(dc,bitmap);check(previous!=0,8506);
    for(U format=0;format<2;++format) {
        write_file(path,format?shell_ne:shell_pe,sizeof shell_pe);
        check(ExtractIconA(0,path,~0u)==2,8507);
        check(ExtractIconExA(path,0,0,0,0)==2,8508);
        U large=0,small=0;
        check(ExtractIconExA(path,0,&large,&small,1)==2 && large && small && large!=small,8509);
        draw(dc,large,0xb74a12);draw(dc,small,0xb74a12);
        check(DeleteFileA(path),8510);draw(dc,large,0xb74a12);draw(dc,small,0xb74a12);
        check(DestroyIcon(large) && DestroyIcon(small),8511);
        check(!DrawIconEx(dc,0,0,large,32,32,0,0,3),8512);
        write_file(path,format?shell_ne:shell_pe,sizeof shell_pe);
        U handles[4]={0x12345678,0x23456789,0x3456789a,0x456789ab};
        check(ExtractIconEx(path,0,handles,0,2)==2 && handles[0] && handles[1] && handles[2]==0x3456789a,8513);
        draw(dc,handles[0],0xb74a12);draw(dc,handles[1],0x2a81e7);
        check(DestroyIcon(handles[0]) && DestroyIcon(handles[1]),8514);
        U by_id=ExtractIconA(0,path,(U)-101);check(by_id,8515);draw(dc,by_id,0x2a81e7);check(DestroyIcon(by_id),8516);
        check(!ExtractIconA(0,path,(U)-999) && !ExtractIconA(0,path,2),8517);
        check(SetEnvironmentVariableA("NEO98_ICON_FILE",path),8518);
        U expanded=ExtractIconA(0,"%NEO98_ICON_FILE%",0);check(expanded,8519);draw(dc,expanded,0xb74a12);check(DestroyIcon(expanded),8520);
        check(SetEnvironmentVariableA("NEO98_ICON_FILE",0),8521);
        for(U i=0;i<24;++i) { U icon=ExtractIconA(0,path,i&1);check(icon,8522);check(DestroyIcon(icon),8523); }
        check(DeleteFileA(path),8524);
        handles[0]=0x1234;handles[1]=0x5678;
        check(ExtractIconExA(path,0,handles,0,2)==~0u && handles[0]==0 && handles[1]==0x5678,8525);
        check(ExtractIconA(0,path,~0u)==0,8526);
    }
    path="C:\\NEO98-SHELL-ICONS.ICO";write_file(path,shell_ico,sizeof shell_ico);
    check(ExtractIconA(0,path,~0u)==1,8527);
    U large=0,small=0;check(ExtractIconExA(path,0,&large,&small,1)==1 && large && small && large!=small,8528);
    check(DeleteFileA(path),8529);draw(dc,large,0xb74a12);draw(dc,small,0xb74a12);
    check(DestroyIcon(large) && DestroyIcon(small),8530);
    check(SelectObject(dc,previous)==bitmap && DeleteObject(bitmap) && DeleteDC(dc),8531);
}
#ifdef SHELL_ICONS_STANDALONE
void mainCRTStartup(void) { run_shell_icons_test();ExitProcess(0); }
#endif
