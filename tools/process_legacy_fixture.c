/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
typedef unsigned U;typedef struct{U hwnd,message,wp,lp,time;int x,y;} MSG;typedef struct{U style,proc,extra,classExtra,instance,icon,cursor,brush;const char*menu,*name;} CLASS;
#define API __declspec(dllimport) __stdcall
API U WinExec(const char*,U);API U LoadModule(const char*,U*);API void GetStartupInfoA(U*);API U GetLastError(void);API U SetLastError(U);API void ExitProcess(U);API char*GetCommandLineA(void);API U GetModuleHandleA(const char*);API U GetEnvironmentVariableA(const char*,char*,U);API U SetEnvironmentVariableA(const char*,const char*);API U GetCurrentDirectoryA(U,char*);API U SetCurrentDirectoryA(const char*);API void Sleep(U);
API U CreateFileA(const char*,U,U,U,U,U,U);API U WriteFile(U,const void*,U,U*,U);API U ReadFile(U,void*,U,U*,U);API U CloseHandle(U);API U GetFileAttributesA(const char*);
API U RegisterClassA(CLASS*);API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,U);API U DefWindowProcA(U,U,U,U);API U GetMessageA(MSG*,U,U,U);API U DispatchMessageA(MSG*);API U FindWindowA(const char*,const char*);API U PostMessageA(U,U,U,U);
void*memset(void*p,int v,U n){char*q=p;for(U i=0;i<n;++i)q[i]=v;return p;}
static int same(const char*a,const char*b){while(*a&&*a==*b)++a,++b;return *a==*b;}
static int has(const char*s,const char*part){for(;*s;++s){const char*a=s,*b=part;while(*a&&*a==*b)++a,++b;if(!*b)return 1;}return 0;}
static void check(U v,U code){if(!v)ExitProcess(code);}
static void write(const char*name,U value){U n,h=CreateFileA(name,0x40000000,3,0,2,0,0);check(h!=~0u,18700);check(WriteFile(h,&value,4,&n,0)&&n==4&&CloseHandle(h),18701);}
static U read(const char*name){U n,v=0,h=CreateFileA(name,0x80000000,3,0,3,0,0);check(h!=~0u&&ReadFile(h,&v,4,&n,0)&&n==4&&CloseHandle(h),18702);return v;}
static U __stdcall proc(U h,U m,U wp,U lp){if(m==0x10){ExitProcess(0);return 0;}return DefWindowProcA(h,m,wp,lp);}
void mainCRTStartup(void){char*line=GetCommandLineA();U si[17]={0};char text[260];
 if(line[0]=='P'){
  GetStartupInfoA(si);check(si[0]==68&&si[11]==0&&si[12]==1,18703);GetStartupInfoA((U*)1);check(GetLastError()==998,18704);
  check(SetEnvironmentVariableA("PARENT","inherited")&&SetCurrentDirectoryA("C:\\WINDOWS\\TEMP"),18705);
  check(WinExec("NO-SUCH-LEGACY.EXE",7)==2,18706);
  check(WinExec("LEGACY.EXE gui",7)>32,18707);
  // This file is written by the child before it actually blocks in GetMessage.
  check(read("C:\\WINDOWS\\TEMP\\GUI-READY.DAT")==7,18708);
  check(GetCurrentDirectoryA(260,text)&&same(text,"C:\\WINDOWS\\TEMP"),18709);
  unsigned short show[2]={1,9};char tail[]={6,'l','o','a','d','e','d',0};char env[]="LEGACY=child\0PATH=C:\\WINDOWS\0";U block[4]={(U)env,(U)tail,(U)show,0};
  check(LoadModule("LEGACY.EXE",block)==0,18710);show[0]=2;tail[0]=200;
  check(!LoadModule("LEGACY.EXE",block)&&GetLastError()==87,18711);tail[0]=6;
  check(LoadModule("NO-SUCH-LEGACY.EXE",block)==2,18712);
  check(LoadModule("LEGACY.EXE",block)==32,18713);
  env[0]='X';tail[1]='X';show[1]=99; // Child owns its environment/startup/command.
  unsigned char empty[1]={0};show[1]=3;block[0]=0;block[1]=(U)empty;
  check(LoadModule("LEGACY.EXE",block)==32,18714);
  while(GetFileAttributesA("C:\\WINDOWS\\TEMP\\LOAD-READY.DAT")==~0u||GetFileAttributesA("C:\\WINDOWS\\TEMP\\EMPTY-READY.DAT")==~0u)Sleep(0);
  check(read("C:\\WINDOWS\\TEMP\\LOAD-READY.DAT")==9&&read("C:\\WINDOWS\\TEMP\\EMPTY-READY.DAT")==3,18715);
  U child=FindWindowA("LEGACY_GUI_CHILD",0);check(child&&PostMessageA(child,0x10,0,0),18716);
  while(FindWindowA("LEGACY_GUI_CHILD",0))Sleep(0);ExitProcess(0);
 }
 GetStartupInfoA(si);check(si[0]==68&&si[11]==1,18717);check(GetCurrentDirectoryA(260,text)&&same(text,"C:\\WINDOWS\\TEMP"),18718);
 if(has(line,"gui")){
  check(si[12]==7&&GetEnvironmentVariableA("PARENT",text,260)&&same(text,"inherited"),18719);
  U instance=GetModuleHandleA(0);CLASS c={0,(U)proc,0,0,instance,0,0,6,0,"LEGACY_GUI_CHILD"};check(RegisterClassA(&c),18720);check(CreateWindowExA(0,c.name,"WinExec child",0x10cf0000,50,60,280,120,0,0,instance,0),18721);write("C:\\WINDOWS\\TEMP\\GUI-READY.DAT",7);
  MSG m;while(GetMessageA(&m,0,0,0))DispatchMessageA(&m);ExitProcess(18722);
 }
 if(has(line,"loaded")){
  check(same(line,"LEGACY.EXE loaded")&&si[12]==9&&GetEnvironmentVariableA("LEGACY",text,260)&&same(text,"child")&&!GetEnvironmentVariableA("PARENT",text,260),18723);write("C:\\WINDOWS\\TEMP\\LOAD-READY.DAT",9);ExitProcess(0);
 }
 check(same(line,"C:\\WINDOWS\\LEGACY.EXE")&&si[12]==3,18724);write("C:\\WINDOWS\\TEMP\\EMPTY-READY.DAT",3);ExitProcess(0);
}
