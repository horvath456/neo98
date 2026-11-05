// SPDX-License-Identifier: GPL-2.0-or-later
// Integration checks against the installed Windows data, without compiled fixtures.
typedef unsigned U;
#define API __declspec(dllimport)
#define CALL __stdcall
API void CALL ExitProcess(U);
API U CALL FormatMessageA(U,const void*,U,U,char*,U,void*);
API int CALL MultiByteToWideChar(U,U,const char*,int,unsigned short*,int);
API U CALL GlobalAlloc(U,U);
API void *CALL GlobalLock(U);
API U CALL GlobalUnlock(U);
API U CALL OpenClipboard(U);
API U CALL EmptyClipboard(void);
API U CALL SetClipboardData(U,U);
API U CALL GetClipboardData(U);
API U CALL CloseClipboard(void);
API U CALL LoadCursorA(U,const char*);
API U CALL LoadIconA(U,const char*);
API U CALL CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,void*);
API U CALL GetSystemMenu(U,int);
API int CALL GetMenuItemCount(U);
API int CALL GetMenuStringA(U,U,char*,int,U);
API U CALL DestroyWindow(U);
API U CALL GetDefaultCommConfigA(const char*,void*,U*);
API U CALL CommConfigDialogA(const char*,U,void*);
API U CALL GetLastError(void);
API int CALL MessageBoxA(U,const char*,const char*,U);
API U CALL TrackPopupMenu(U,U,int,int,int,U,const void*);
void mainCRTStartup(void) {
    char message[1024]; unsigned short wide[8];
    if (!FormatMessageA(0x1200,0,2,0,message,sizeof message,0) || !message[0]) ExitProcess(11);
    MessageBoxA(0,message,"Neo98 system message",0);
    if (MultiByteToWideChar(1252,0,"\200\344",3,wide,8)!=3 || wide[0]!=0x20ac || wide[1]!=0xe4) ExitProcess(12);
    if (!LoadCursorA(0,(const char*)32512) || !LoadIconA(0,(const char*)32512)) ExitProcess(13);
    U window=CreateWindowExA(0,"STATIC","Neo98 runtime data test",0x10cf0000,0,0,320,200,0,0,0,0);
    if (!window) ExitProcess(14);
    U menu=GetSystemMenu(window,0);
    if (!menu || GetMenuItemCount(menu)<5 || !GetMenuStringA(menu,0xf060,message,sizeof message,0)) ExitProcess(15);
    TrackPopupMenu(menu,0x100,100,100,0,window,0);
    if (!OpenClipboard(window) || !EmptyClipboard()) ExitProcess(16);
    U memory=GlobalAlloc(0x42,3); char *text=(char*)GlobalLock(memory);
    if (!text) ExitProcess(17);
    text[0]=(char)0xe4;text[1]=0;text[2]=0;GlobalUnlock(memory);
    if (!SetClipboardData(1,memory)) ExitProcess(18);
    if (!CloseClipboard() || !OpenClipboard(window)) ExitProcess(22);
    U converted=GetClipboardData(7); unsigned char *oem=(unsigned char*)GlobalLock(converted);
    if (!oem || oem[0]!=0x84 || oem[1]) ExitProcess(19);
    GlobalUnlock(converted); CloseClipboard(); DestroyWindow(window);
#ifndef RELEASE_DATA_SKIP_SERIALUI
    unsigned char config[52]; U size=sizeof config;
    if (!GetDefaultCommConfigA("COM1",config,&size)) ExitProcess(20);
    if (CommConfigDialogA("COM1",0,config) || GetLastError()!=1223) ExitProcess(21);
#endif
    ExitProcess(0);
}
