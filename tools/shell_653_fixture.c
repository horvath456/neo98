/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
typedef unsigned U;
void*memset(void*p,int c,U n){unsigned char*q=p;for(U i=0;i<n;++i)q[i]=(unsigned char)c;return p;}
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API char*GetCommandLineA(void);API U SetEnvironmentVariableA(const char*,const char*);API U SetCurrentDirectoryA(const char*);
API U O653(const char*,char*,U,U);API U O51(char*,const char*const*,U);API U O40(const char*);
API U O3(char*,U);API U O5(char*,const char*const*,U);API U PathFindOnPathA(char*,const char*const*);API U PathFileExistsA(const char*);
static void check(U v,U e){if(!v)ExitProcess(e);}
static U len(const char*p){U n=0;while(p[n])++n;return n;}
static void copy(char*d,const char*s){while((*d++=*s++));}
static U equal(const char*a,const char*b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
void mainCRTStartup(void){
 char path[260]={0},out[1024]={0};const char*dirs[]={"C:\\FIRST",0};
 check(O40("tool")==1&&O40("C:\\tool")==0&&O40("\\tool")==0,13700);
 check(SetCurrentDirectoryA("C:\\TOOLS"),13701);check(SetEnvironmentVariableA("PATH","C:\\SECOND;C:\\TOOLS"),13702);
 check(PathFileExistsA("tool.exe")&&!PathFileExistsA("missing.exe"),13703);
 copy(path,"C:\\TOOLS\\tool");check(O3(path,63)&&equal(path,"C:\\TOOLS\\tool.exe"),13704);
 copy(path,"tool");check(O5(path,dirs,63)&&equal(path,"C:\\FIRST\\tool.pif"),13705);
 copy(path,"tool.exe");check(PathFindOnPathA(path,0)&&equal(path,"C:\\SECOND\\tool.exe"),13706);
 check(SetEnvironmentVariableA("PATH","C:\\TOOLS;C:\\SECOND"),13707);copy(path,"tool.exe");check(PathFindOnPathA(path,0)&&equal(path,"C:\\TOOLS\\tool.exe"),13708);
 copy(path,"tool");check(O51(path,0,3)&&equal(path,"C:\\TOOLS\\tool.exe"),13709);
 copy(path,"thing");check(O5(path,0,63)&&equal(path,"C:\\WINDOWS\\SYSTEM\\thing.com"),13710);
 const char*cmd="C:\\My Tools\\paint.exe -a";U needed=O653(cmd,0,0,1);check(needed==len("\"C:\\My Tools\\paint.exe\" -a")+1,13711);copy(out,"sentinel");check(O653(cmd,out,4,1)==(U)-1&&equal(out,"sentinel"),13712);check(O653(cmd,out,sizeof out,1)==needed&&equal(out,"\"C:\\My Tools\\paint.exe\" -a"),13713);
 check(O653("\"C:\\missing.exe\" -a",out,sizeof out,0)==len("C:\\missing.exe")+1&&equal(out,"C:\\missing.exe"),13714);
 check(O653("C:\\missing.exe -a",out,sizeof out,0)==(U)-1,13715);
 copy(path,"C:\\My Tools\\paint.exe -a");check(O653(path,path,sizeof path,1)==needed&&equal(path,"\"C:\\My Tools\\paint.exe\" -a"),13716);
 copy(path,"C:\\My Tools");check(O51(path,0,1),13717);check(O653("C:\\My Tools",out,sizeof out,0x10)==(U)-1,13718);
 copy(path,"missing");check(!O5(path,0,63)&&equal(path,"missing"),13719);
 ExitProcess(0);
}
