/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
typedef unsigned U;
void*memset(void*p,int c,U n){unsigned char*q=p;for(U i=0;i<n;++i)q[i]=(unsigned char)c;return p;}
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetLastError(void);API U SetEnvironmentVariableA(const char*,const char*);API U SetCurrentDirectoryA(const char*);
API const char* GetCommandLineA(void);API void GetStartupInfoA(U*);API U WaitForSingleObject(U,U);API U GetExitCodeProcess(U,U*);API U CloseHandle(U);
API U CreateFileA(const char*,U,U,U,U,U,U);API U WriteFile(U,const void*,U,U*,U);
API U ShellExecuteA(U,const char*,const char*,const char*,const char*,U);API U ShellExecuteExA(U*);
API U RegCreateKeyA(U,const char*,U*);API U RegSetValueExA(U,const char*,U,U,const char*,U);API U RegCloseKey(U);
static void check(U v,U e){if(!v)ExitProcess(e);}
static U len(const char*p){U n=0;while(p[n])++n;return n;}
static U same(const char*a,const char*b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
static void value(U root,const char*path,const char*name,const char*text){U k=0;check(!RegCreateKeyA(root,path,&k),14300);check(!RegSetValueExA(k,name,0,1,text,len(text)+1),14301);check(!RegCloseKey(k),14302);}
void mainCRTStartup(void){
 const char*line=GetCommandLineA();
 if(!same(line,"SHELLEXEC.EXE")){
  const char*marker=0;U show=1;
  /* The hive's exefile\shell\open\command template is "%1" %*, and the original
     expansion at keeps the template's literal space while %* expands to
     nothing, so a parameterless launch quotes the path and keeps one trailing space. */
  if(same(line,"\"C:\\TOOLS\\tool.exe\" "))marker="C:\\TOOLS\\direct.done";
  if(same(line,"\"C:\\TOOLS\\tool.exe\" --alias"))marker="C:\\TOOLS\\alias.done";
  if(same(line,"tool.exe /edit \"C:\\TOOLS\\document.xyz\" first arg /show:3")){marker="C:\\TOOLS\\edit.done";show=3;}
  if(same(line,"tool.exe /changed C:\\TOOLS\\document.xyz"))marker="C:\\TOOLS\\changed.done";
  check(marker!=0,14315);U startup[17]={};GetStartupInfoA(startup);check(startup[11]==1&&(startup[12]&65535)==show,14316);
  U h=CreateFileA(marker,0x40000000,0,0,2,0,0),written=0;check(h!=~0u&&WriteFile(h,line,len(line),&written,0)&&written==len(line)&&CloseHandle(h),14317);ExitProcess(42);
 }
 check(SetCurrentDirectoryA("C:\\TOOLS"),14303);check(SetEnvironmentVariableA("PATH","C:\\TOOLS"),14304);
 check(ShellExecuteA(0,0,"missing",0,0,1)==2&&GetLastError()==2,14305);
 U info[15]={59,0x440,0,0,(U)"tool.exe",0,0,1,99,0,0,0,0,0,77};check(!ShellExecuteExA(info)&&info[8]==5&&GetLastError()==5&&info[1]==0x440&&info[14]==77,14306);
 info[0]=60;check(ShellExecuteExA(info)&&info[8]>32&&info[1]==0x440&&info[14],14307);
 U code=0;check(WaitForSingleObject(info[14],5000)==0&&GetExitCodeProcess(info[14],&code)&&code==42&&CloseHandle(info[14]),14318);
 check(ShellExecuteA(0,"print","tool.exe",0,0,1)==31&&GetLastError()==1155,14308);
 value(0x80000002,"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\alias.exe",0,"C:\\TOOLS\\tool.exe");
 check(ShellExecuteA(0,0,"alias","--alias",0,1)>32,14309);
 value(0x80000000,".xyz",0,"Neo98.Document");value(0x80000000,"Neo98.Document\\shell",0,"edit");value(0x80000000,"Neo98.Document\\shell\\edit\\command",0,"tool.exe /edit \"%L\" %2 /show:%S");
 check(ShellExecuteA(0,0,"document.xyz","\"first arg\" rest",0,3)>32,14310);
 value(0x80000000,"Neo98.Document\\shell\\edit\\command",0,"tool.exe /changed %L");check(ShellExecuteA(0,0,"document.xyz",0,0,1)>32,14311);
 value(0x80000000,"Neo98.Document\\shell\\edit\\ddeexec",0,"[open(%1)]");check(ShellExecuteA(0,0,"document.xyz",0,0,1)==5&&GetLastError()==120,14312);
 value(0x80000000,"Neo98.Document\\shell\\edit","CLSID","{00000000-0000-0000-0000-000000000001}");check(ShellExecuteA(0,0,"document.xyz",0,0,1)==5&&GetLastError()==120,14313);
 check(ShellExecuteA(0,0,"https://example.invalid",0,0,1)==5&&GetLastError()==120,14314);
 ExitProcess(0);
}
