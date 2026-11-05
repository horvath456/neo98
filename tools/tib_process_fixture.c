/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Win9x TIB+30h identifies a shared process object, not an NT PEB. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API char *GetCommandLineA(void);
API U GetModuleFileNameA(U, char *, U);
API U GetModuleHandleA(const char *);
API U CreateThread(void *, U, U (__stdcall *)(void *), void *, U, U *);
API U GetExitCodeThread(U, U *);
API U CreateProcessA(const char *, char *, void *, void *, U, U, void *, const char *, void *, void *);
API U WaitForSingleObject(U, U);
API U GetExitCodeProcess(U, U *);
API U CloseHandle(U);
void *memset(void *p, int v, U n) { unsigned char *q=p; while(n--) *q++=(unsigned char)v; return p; }
static U pdb(void) { U p; __asm__ volatile("movl %%fs:0x30,%0" : "=r"(p)); return p; }
static U __stdcall worker(void *p) { return pdb()==(U)p ? 73 : 10; }
void mainCRTStartup(void)
{
    U p=pdb(), code=0;
    /* This is the branch the original packed installer takes before any API. */
    if ((int)p>=0) ExitProcess(11);
    if (p>=0xc0000000 || *(unsigned char *)p!=6) ExitProcess(12);
    if (!GetModuleHandleA("KERNEL32.DLL")) ExitProcess(13);
    /* Win98 reference: no handler invocation and EAX stays 4300h. */
    U debugger = 0x4300;
    __asm__ volatile("int $0x68" : "+a"(debugger) :: "memory");
    if (debugger != 0x4300) ExitProcess(19);
    char *command=GetCommandLineA();
    if (command[0]=='c') {
        U parent=0;
        for (U i=1;i<9;++i) parent=(parent<<4)+(command[i]<='9' ? command[i]-'0' : command[i]-'a'+10);
        if (parent==p || *(unsigned char *)parent!=6) ExitProcess(14);
        ExitProcess(73);
    }
    U thread=CreateThread(0,0,worker,(void *)p,0,0);
    if (!thread || WaitForSingleObject(thread,10000) || !GetExitCodeThread(thread,&code) || code!=73) ExitProcess(15);
    CloseHandle(thread);
    char path[260], child[10]="c00000000";
    U si[17]={68}, pi[4]={0};
    for (U i=0;i<8;++i) child[i+1]="0123456789abcdef"[(p>>((7-i)*4))&15];
    if (!GetModuleFileNameA(0,path,sizeof path)) ExitProcess(16);
    if (!CreateProcessA(path,child,0,0,0,0,0,0,si,pi)) ExitProcess(17);
    if (WaitForSingleObject(pi[0],10000) || !GetExitCodeProcess(pi[0],&code) || code!=73) ExitProcess(18);
    CloseHandle(pi[0]); CloseHandle(pi[1]);
    ExitProcess(0);
}
