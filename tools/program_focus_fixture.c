/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* guest-side launches while Moorhuhn owns the exclusive display. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API void Sleep(U);
API U CreateProcessA(const char *,char *,void *,void *,U,U,void *,const char *,void *,void *);
API U CloseHandle(U);
API U GetForegroundWindow(void);
API U GetWindowThreadProcessId(U,U *);
API U GetWindowLongA(U,int);
API U GetWindowRect(U,int *);
API U FindWindowA(const char *,const char *);
API U CreateFileA(const char *,U,U,void *,U,U,U);
API U WriteFile(U,void *,U,U *,void *);
static U start(char *command,const char *directory){
 U startup[17]={68},process[4]={0};
 if(!CreateProcessA(0,command,0,0,0,0,0,directory,startup,process))ExitProcess(11);
 CloseHandle(process[0]);CloseHandle(process[1]);return process[2];
}
static U ids[5];
static void record(const char *path){
 U r[14]={0},written=0;
 for(U i=0;i<5;++i)r[i]=ids[i];
 r[5]=GetForegroundWindow();GetWindowThreadProcessId(r[5],&r[6]);
 r[7]=FindWindowA("Moorhuhn2",0);
 if(!r[7])r[7]=FindWindowA(0,"Moorhuhn 2");
 r[8]=GetWindowLongA(r[7],-16);GetWindowRect(r[7],(int *)&r[9]);
 U file=CreateFileA(path,0x40000000,0,0,2,0,0);
 WriteFile(file,r,sizeof r,&written,0);CloseHandle(file);
}
void mainCRTStartup(void){
 char a[]="C:\\WINDOWS\\NOTEPAD.EXE",b[]="C:\\WINDOWS\\CALC.EXE";
 char c[]="D:\\GAMES\\MAHJONG\\MJWIN.EXE",d[]="C:\\MH2\\Moorhuhn2.exe";
 char e[]="D:\\GAMES\\SPONGEB\\sb2.exe";
 ids[0]=start(a,0);Sleep(1500);ids[1]=start(b,0);Sleep(1500);
 ids[2]=start(c,"D:\\GAMES\\MAHJONG");Sleep(2000);
 ids[3]=start(d,"C:\\MH2");Sleep(15000);record("C:\\FOCUS1.BIN");
 ids[4]=start(e,"D:\\GAMES\\SPONGEB");Sleep(10000);record("C:\\FOCUS2.BIN");
 Sleep(15000);ExitProcess(0);
}
