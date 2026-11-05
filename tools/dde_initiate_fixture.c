/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A shell-style DDE discovery broadcast while SYSDM.CPL owns Win16 windows. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U GetModuleHandleA(const char *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U,const char *,const char *,U,int,int,int,int,U,U,U,U);
API U DefWindowProcA(U,U,U,U);
API U GlobalAddAtomA(const char *);
API U GlobalDeleteAtom(U);
API U SendMessageTimeoutA(U,U,U,U,U,U,U *);
API U FindWindowA(const char *,const char *);
API U FindWindowExA(U,U,const char *,const char *);
API U GetWindow(U,U);
API U PostMessageA(U,U,U,U);
API U CreateProcessA(const char *,char *,U,U,U,U,U,U,void *,void *);
API void Sleep(U);
API void OutputDebugStringA(const char *);
API void ExitProcess(U);

void mainCRTStartup(void)
{
    U instance=GetModuleHandleA(0), result=0;
    U wc[10]={0,(U)DefWindowProcA,0,0,instance,0,0,0,0,(U)"DdeInitiateProbe"};
    if (!RegisterClassA(wc)) ExitProcess(1);
    U window=CreateWindowExA(0,"DdeInitiateProbe","",0,0,0,0,0,0,0,instance,0);
    U startup[17]={68}, process[4]={0}, sheet=0;
    char command[]="rundll32.exe shell32.dll,Control_RunDLL sysdm.cpl";
    if (!CreateProcessA("C:\\WINDOWS\\RUNDLL32.EXE",command,0,0,0,0,0,0,startup,process)) ExitProcess(3);
    for (int i=0;i<200 && !sheet;++i) {
        Sleep(50);
        sheet=FindWindowA("#32770","System Properties");
    }
    if (!sheet) ExitProcess(4);
    Sleep(500);
    /* The OEM support sheet is a genuine Win16 top-level dialog. The outer
       System Properties sheet itself is hosted by the Win32 property sheet. */
    U page=FindWindowExA(sheet,0,"#32770",0);
    U button=FindWindowExA(page,0,"BUTTON",0), support=0;
    if (!button) ExitProcess(6);
    PostMessageA(button,0xf5,0,0);
    for (int i=0;i<100 && !support;++i) {
        Sleep(50);
        for(U h=FindWindowExA(0,0,"#32770",0);h;h=FindWindowExA(0,h,"#32770",0))
            if(GetWindow(h,4)==sheet) { support=h;break; }
    }
    if (!support) ExitProcess(7);
    OutputDebugStringA("DDE System Properties ready\n");
    U app=GlobalAddAtomA("NEO98DDEPROBE"), topic=GlobalAddAtomA("WWW_OpenURL");
    if (!window || !app || !topic) ExitProcess(2);
    SendMessageTimeoutA(0xffff,0x3e0,window,app|(topic<<16),2,1000,&result);
    GlobalDeleteAtom(topic);
    GlobalDeleteAtom(app);
    OutputDebugStringA("DDE broadcast returned\n");
    PostMessageA(support,0x10,0,0);Sleep(300);
    PostMessageA(sheet,0x10,0,0);
    for (int i=0;i<100 && FindWindowA("#32770","System Properties");++i) Sleep(50);
    if (FindWindowA("#32770","System Properties")) ExitProcess(5);
    OutputDebugStringA("DDE System Properties closed\n");
    ExitProcess(0);
}
