/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U CreateProcessA(const char*,char*,U,U,U,U,const char*,const char*,U*,U*);
API U GetExitCodeProcess(U,U*);API U GetExitCodeThread(U,U*);API U WaitForSingleObject(U,U);API U CloseHandle(U);API U GetLastError(void);API void ExitProcess(U);API void ExitThread(U);API char*GetCommandLineA(void);API U GetCurrentProcessId(void);API U GetCurrentThreadId(void);API U GetStartupInfoA(U*);API U GetEnvironmentVariableA(const char*,char*,U);API U SetEnvironmentVariableA(const char*,const char*);API U GetCurrentDirectoryA(U,char*);API U SetCurrentDirectoryA(const char*);API U VirtualAlloc(U,U,U,U);API U CreateThread(U,U,U,U,U,U*);API void Sleep(U);
API U OpenProcess(U,U,U);API U WaitForMultipleObjects(U,const U*,U,U);
API U CreateEventA(U,U,U,const char*);API U CreateFileA(const char*,U,U,U,U,U,U);API U WriteFile(U,const void*,U,U*,U);API U ReadFile(U,void*,U,U*,U);API U DeleteFileA(const char*);API U GetFileAttributesA(const char*);
void*memset(void*p,int v,U n){char*q=p;for(U i=0;i<n;++i)q[i]=v;return p;}
static int same(const char*a,const char*b){while(*a&&*a==*b)++a,++b;return *a==*b;}
static void check(U v,U code){if(!v)ExitProcess(code);}
static void write(const char*name,U value){U n,h=CreateFileA(name,0x40000000,3,0,2,0,0);check(h!=~0u,16300);check(WriteFile(h,&value,4,&n,0)&&n==4&&CloseHandle(h),16301);}
static U read(const char*name){U n,v=0,h=CreateFileA(name,0x80000000,3,0,3,0,0);check(h!=~0u&&ReadFile(h,&v,4,&n,0)&&n==4&&CloseHandle(h),16302);return v;}
static volatile U*secret;
static U __stdcall worker(U p){while(GetFileAttributesA("C:\\WINDOWS\\TEMP\\RELEASE.DAT")==~0u)Sleep(0);check(*secret==0x22223333,16303);write("C:\\WINDOWS\\TEMP\\ANSWER.DAT",GetCurrentProcessId());ExitProcess(42);return p;}
void mainCRTStartup(void){char*line=GetCommandLineA();char text[260];U si[17]={68},pi[4]={0},v=0,n=0;
 if(line[0]=='P'){
  secret=(volatile U*)VirtualAlloc(0x18000000,4096,0x3000,4);check((U)secret==0x18000000,16304);*secret=0x11112222;
  check(SetEnvironmentVariableA("PARENT","inherited"),16305);
  char command[]="C:\\CHILD DIR\\CHILD.EXE payload";char env[]="CHILD=explicit\0PATH=C:\\WINDOWS\0";char title[]="child title";si[3]=(U)title;si[11]=1;si[12]=7;
  // DETACHED_PROCESS still needs a console service; CREATE_SUSPENDED is
  // supported (tools/test_create_suspended_host.py).
  check(!CreateProcessA("C:\\CHILD DIR\\CHILD.EXE",command,0,0,0,8,env,"C:\\WINDOWS\\TEMP",si,pi)&&GetLastError()==50,16306);
  // An inheritable handle to a named kernel object no longer stops the
  // launch: Windows 98 copies those entries into the child. The call therefore
  // has to get past the flag check and fail on the absent program instead.
  // tools/named_objects_fixture.c checks what the child then sees.
  U sa[3]={12,0,1},inheritable=CreateEventA((U)sa,1,0,0);check(inheritable,16336);
  check(!CreateProcessA("C:\\ABSENT.EXE",command,0,0,1,0,env,"C:\\WINDOWS\\TEMP",si,pi)&&GetLastError()==2,16307);
  check(CloseHandle(inheritable),16337);
  check(!CreateProcessA("C:\\ABSENT.EXE",command,0,0,0,0,env,"C:\\WINDOWS\\TEMP",si,pi)&&GetLastError()==2,16308);
  // Windows 98 reads hStdInput, hStdOutput and hStdError only when dwFlags has
  // STARTF_USESTDHANDLES and neither STARTF_USEHOTKEY nor STARTF_HASSHELLDATA;
  // otherwise the child inherits the parent's three handles and the fields
  // stay untouched. A caller that leaves the rest of its STARTUPINFOA
  // uninitialised, as the original DDRAW.DLL does, must get its child.
  si[14]=0x12345678;si[15]=0xdeadbeefu;si[16]=0x0badf00d;
  si[11]=1|0x100;
  check(!CreateProcessA(0,command,0,0,0,0,env,"C:\\WINDOWS\\TEMP",si,pi)&&GetLastError()==50,16335);
  si[11]=1;
  check(CreateProcessA(0,command,0,0,0,0,env,"C:\\WINDOWS\\TEMP",si,pi),16309);
  check(pi[0]&&pi[1]&&pi[0]!=pi[1]&&pi[2]!=GetCurrentProcessId()&&pi[3]!=GetCurrentThreadId(),16310);
  check(!GetExitCodeProcess(pi[0],(U*)1)&&GetLastError()==998,16334);
  check(GetExitCodeProcess(pi[0],&v)&&v==259&&GetExitCodeThread(pi[1],&v)&&v==259,16311);
  check(WaitForSingleObject(pi[0],0)==258&&WaitForSingleObject(pi[1],0)==258,16312);
  check(!GetExitCodeProcess(pi[1],&v)&&GetLastError()==6&&!GetExitCodeThread(pi[0],&v)&&GetLastError()==6,16313);
  command[0]='X';env[0]='X';title[0]='X';si[12]=2;check(SetCurrentDirectoryA("C:\\WINDOWS"),16314);
  write("C:\\CHILD DIR\\CHILD.EXE",0); // launch owns its complete PE snapshot
  check(WaitForSingleObject(pi[1],~0u)==0,16315);
  check(GetExitCodeThread(pi[1],&v)&&v==41&&GetExitCodeProcess(pi[0],&v)&&v==259,16316);
  check(*secret==0x11112222,16317);write("C:\\WINDOWS\\TEMP\\RELEASE.DAT",1);
  check(WaitForSingleObject(pi[0],~0u)==0&&GetExitCodeProcess(pi[0],&v)&&v==42,16318);
  check(read("C:\\WINDOWS\\TEMP\\ANSWER.DAT")==pi[2],16319);
  check(CloseHandle(pi[0])&&!CloseHandle(pi[0])&&GetLastError()==6&&GetExitCodeThread(pi[1],&v)&&v==41&&CloseHandle(pi[1]),16320);
  check(GetEnvironmentVariableA("PARENT",text,260)&&same(text,"inherited"),16321);ExitProcess(0);
 }
 if(line[0]=='O'){
  check(SetEnvironmentVariableA("PARENT","inherited"),16322);char cmd[]="detached child";
  check(CreateProcessA("C:\\CHILD DIR\\CHILD.EXE",cmd,0,0,0,0,0,0,si,pi),16323);
  // Handles cannot own the child's lifetime. One is explicitly closed; the
  // other remains open for the parent destructor to release.
  check(CloseHandle(pi[0]),16324);ExitProcess(0);
 }
 // The DDHELP.EXE shape of a helper watches the process that started
 // it. It has no child handle, only the identity, so it opens the process
 // itself and waits on that handle. Win98 signals such a handle when the
 // process ends and answers its exit code afterwards.
 if(line[0]=='F'){
  write("C:\\WINDOWS\\TEMP\\PARENT-ID.DAT",GetCurrentProcessId());char watcher[]="foreign watcher";
  check(CreateProcessA("C:\\CHILD DIR\\CHILD.EXE",watcher,0,0,0,0,0,0,si,pi),16338);
  check(CloseHandle(pi[0])&&CloseHandle(pi[1]),16339);
  while(GetFileAttributesA("C:\\WINDOWS\\TEMP\\WATCHING.DAT")==~0u)Sleep(0);
  ExitProcess(55);
 }
 if(line[0]=='f'){
  U parent=read("C:\\WINDOWS\\TEMP\\PARENT-ID.DAT"),handle;
  check(parent&&parent!=GetCurrentProcessId(),16340);
  check(!OpenProcess(0x100400,0,0x7ffe),16341);            // no such process: null, no error path
  handle=OpenProcess(0x100400,0,parent);check(handle,16342);
  check(WaitForSingleObject(handle,0)==258,16343);         // alive, so not signalled
  check(GetExitCodeProcess(handle,&v)&&v==259,16344);
  write("C:\\WINDOWS\\TEMP\\WATCHING.DAT",1);
  check(WaitForMultipleObjects(1,&handle,1,~0u)==0,16345);
  check(WaitForSingleObject(handle,0)==0,16346);           // a dead process stays signalled
  check(GetExitCodeProcess(handle,&v)&&v==55,16347);
  check(CloseHandle(handle)&&!CloseHandle(handle)&&GetLastError()==6,16348);
  write("C:\\WINDOWS\\TEMP\\FOREIGN.DAT",55);ExitProcess(55);
 }
 if(line[0]=='d'){
  while(GetFileAttributesA("C:\\WINDOWS\\TEMP\\ORPHAN-GO.DAT")==~0u)Sleep(0);
  check(GetEnvironmentVariableA("PARENT",text,260)&&same(text,"inherited"),16325);
  check(GetCurrentDirectoryA(260,text)&&same(text,"C:\\"),16326);
  check(SetEnvironmentVariableA("PARENT","child copy"),16327);write("C:\\WINDOWS\\TEMP\\ORPHAN.DAT",77);ExitProcess(77);
 }
 check(same(line,"\"C:\\CHILD DIR\\CHILD.EXE\" payload"),16328);
 GetStartupInfoA(si);check(si[0]==68&&si[11]==1&&si[12]==7&&same((char*)si[3],"child title"),16329);
 check(GetEnvironmentVariableA("CHILD",text,260)&&same(text,"explicit")&&!GetEnvironmentVariableA("PARENT",text,260),16330);
 check(GetCurrentDirectoryA(260,text)&&same(text,"C:\\WINDOWS\\TEMP"),16331);
 secret=(volatile U*)VirtualAlloc(0x18000000,4096,0x3000,4);check((U)secret==0x18000000,16332);*secret=0x22223333;
 U thread=CreateThread(0,0,(U)worker,0,0,&n);check(thread&&CloseHandle(thread),16333);ExitThread(41);
}
