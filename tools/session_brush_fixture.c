// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;
#define API __attribute__((stdcall))
API char *GetCommandLineA(void); API void ExitProcess(U);
API U GetSysColorBrush(U); API U GetSysColor(U); API U GetLastError(void); API void SetLastError(U); API U CreateProcessA(const char*,char*,U,U,U,U,const char*,const char*,U*,U*);
API U WaitForSingleObject(U,U); API U GetExitCodeProcess(U,U*); API U CloseHandle(U);
API U CreateCompatibleDC(U); API U DeleteDC(U); API U CreateDIBSection(U,const void*,U,void**,U,U);
API U SelectObject(U,U); API U FillRect(U,const void*,U); API U GetPixel(U,int,int); API U DeleteObject(U);
static void check(U value,U code){if(!value)ExitProcess(code);}
void mainCRTStartup(void){
 if(GetCommandLineA()[0]=='C'){U brush=GetSysColorBrush(13);check(brush,18101);ExitProcess(brush);}
 // USER32: an index below 0 or from 29 on answers 0 and leaves
 // the last error alone.
 SetLastError(77);check(GetSysColor(28)==GetSysColor(28)&&!GetSysColor(29)&&!GetSysColor(~0u)&&!GetSysColor(0x80000000u)&&GetLastError()==77,18111);
 check(!GetSysColorBrush(29),18112);
 U si[17]={68},pi[4]={0};char command[]="CHILD";
 check(CreateProcessA("C:\\SESSIONBRUSH.EXE",command,0,0,0,0,0,0,si,pi),18102);
 check(WaitForSingleObject(pi[0],~0u)==0,18103);U brush=0;check(GetExitCodeProcess(pi[0],&brush)&&brush,18104);
 check(CloseHandle(pi[0])&&CloseHandle(pi[1]),18105);
 U info[11]={40,2,2,1|(32u<<16),0,16};void*bits=0;U dc=CreateCompatibleDC(0),bitmap=CreateDIBSection(dc,info,0,&bits,0,0);
 check(dc&&bitmap&&bits&&SelectObject(dc,bitmap),18106);int area[4]={0,0,2,2};
 check(FillRect(dc,area,brush)&&GetPixel(dc,0,0)==0x800000,18107);
 check(!DeleteObject(brush),18108);check(FillRect(dc,area,brush)&&GetPixel(dc,1,1)==0x800000,18109);
 check(DeleteDC(dc)&&DeleteObject(bitmap),18110);ExitProcess(0);
}
