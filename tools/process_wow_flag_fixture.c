/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Win98 ignores CREATE_SEPARATE_WOW_VDM for these PE children. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API char *GetCommandLineA(void);
API U GetModuleFileNameA(U, char *, U);
API U GetCurrentProcessId(void);
API U CreateProcessA(const char *, char *, void *, void *, U, U, void *, const char *, void *, void *);
API U WaitForSingleObject(U, U);
API U ResumeThread(U);
API U GetExitCodeProcess(U, U *);
API U GetLastError(void);
API U CloseHandle(U);
void *memset(void *p, int v, U n) { unsigned char *q=p; while(n--) *q++=(unsigned char)v; return p; }
void mainCRTStartup(void)
{
    if (GetCommandLineA()[0]=='c') ExitProcess(73);
    char path[260];
    U si[17]={68}, pi[4]={0}, code;
    const U flags[]={0,0x800,0x810,0x804,0x820};
    if (!GetModuleFileNameA(0,path,sizeof path)) ExitProcess(1);
    for (U i=0;i<sizeof flags/sizeof flags[0];++i) {
        char command[]="child-wow";
        if (!CreateProcessA(path,command,0,0,0,flags[i],0,0,si,pi)) ExitProcess(10+i);
        if (!pi[0] || !pi[1] || !pi[2] || pi[2]==GetCurrentProcessId()) ExitProcess(20+i);
        if (flags[i]&4) {
            if (WaitForSingleObject(pi[0],0)!=258 || ResumeThread(pi[1])!=1) ExitProcess(30+i);
        }
        if (WaitForSingleObject(pi[0],10000)!=0 || !GetExitCodeProcess(pi[0],&code) || code!=73) ExitProcess(40+i);
        if (!CloseHandle(pi[0]) || !CloseHandle(pi[1])) ExitProcess(50+i);
    }
    /* Accepting 0x800 must still run the loader and reject unsupported flags. */
    if (CreateProcessA("E:\\NO-SUCH-WOW.EXE",0,0,0,0,0x800,0,0,si,pi) || GetLastError()!=2) ExitProcess(60);
    if (CreateProcessA(path,0,0,0,0,0x400,0,0,si,pi) || GetLastError()!=50) ExitProcess(61);
    ExitProcess(0);
}
