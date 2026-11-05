/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DEVMODE display flags through both Win98 display entry points. */
#include <windows.h>
static HANDLE report;
static void row(DWORD id,DWORD value){DWORD r[2]={id,value},n;WriteFile(report,r,8,&n,0);}
void mainCRTStartup(void){
 report=CreateFileA("C:\\DISPFLAG.BIN",GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);if(report==INVALID_HANDLE_VALUE)ExitProcess(99);
 const DWORD flags[]={0,1,2,4,8,0xffffffff};
 for(DWORD api=0;api<2;api++)for(DWORD i=0;i<6;i++){
  DEVMODEA dm={0},current={0};dm.dmSize=148;dm.dmFields=DM_BITSPERPEL|DM_PELSWIDTH|DM_PELSHEIGHT|DM_DISPLAYFLAGS;
  dm.dmBitsPerPel=16;dm.dmPelsWidth=640;dm.dmPelsHeight=480;dm.dmDisplayFlags=flags[i];
  DWORD id=api*100+i*10;
  row(id,api?ChangeDisplaySettingsExA(0,&dm,0,CDS_TEST,0):ChangeDisplaySettingsA(&dm,CDS_TEST));
  row(id+1,api?ChangeDisplaySettingsExA(0,&dm,0,CDS_FULLSCREEN,0):ChangeDisplaySettingsA(&dm,CDS_FULLSCREEN));
  current.dmSize=148;row(id+2,EnumDisplaySettingsA(0,ENUM_CURRENT_SETTINGS,&current));
  row(id+3,current.dmPelsWidth);row(id+4,current.dmPelsHeight);row(id+5,current.dmBitsPerPel);
  row(id+6,ChangeDisplaySettingsA(0,0));
 }
 CloseHandle(report);ExitProcess(0);
}
