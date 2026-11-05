/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport)
#define CALL __stdcall
API void CALL ExitProcess(U);
API U CALL GetModuleHandleA(const char *);
API U CALL GetLastError(void);
API void CALL SetLastError(U);
API void CALL InitCommonControls(void);
API U CALL InitCommonControlsEx(const U *);
API U CALL DllGetVersion(U *);
API U CALL GetClassInfoExA(U,const char *,U *);
API U CALL CreateWindowExA(U,const char *,const char *,U,int,int,int,int,U,U,U,void *);
API U CALL DestroyWindow(U);
API U CALL SendMessageA(U,U,U,U);
static void check(int yes,U error){if(!yes)ExitProcess(error);}
int run_common_init_test(void){
    U info[]={8,0},version[]={20,0xa5a5a5a5,0xa5a5a5a5,0xa5a5a5a5,0xa5a5a5a5};
    U module=GetModuleHandleA("COMCTL32.DLL");check(module!=0,9100);
    U attached[12]={48};
    check(GetClassInfoExA(module,"SysTreeView32",attached),9121);
    check(attached[1]==0x4008&&attached[4]==4,9122);
#ifdef COMMON_INIT_STANDALONE
    /* The original module attach requests 0xff, before any public init call. */
    attached[0]=48;check(!GetClassInfoExA(module,"ReBarWindow32",attached),9123);
#endif
    InitCommonControls();
    SetLastError(0x9876);check(InitCommonControlsEx(info)==1,9101);
    check(GetLastError()==0x9876,9102);
    info[0]=7;check(!InitCommonControlsEx(info),9103);
    info[0]=8;info[1]=0x8000;check(!InitCommonControlsEx(info),9104);
    check(!InitCommonControlsEx((void*)0),9105);
    info[1]=0x4000;check(InitCommonControlsEx(info),9106);
    check(DllGetVersion(version)==0,9107);
    check(version[0]==20&&version[1]==5&&version[2]==80&&version[3]==2614&&version[4]==1,9108);
    version[0]=24;version[1]=0xdeadbeef;
    check(DllGetVersion(version)==0x80070057&&version[1]==0xdeadbeef,9109);
    check(DllGetVersion((void*)1)==0x80070057,9110);
    info[1]=0x7fff;check(InitCommonControlsEx(info),9111);
    static const struct {const char *name;U style,extra,cursor;} classes[]={
        {"ToolbarWindow32",0x4008,4,1},{"ReBarWindow32",0x4008,4,0},
        {"tooltips_class32",0x4808,4,1},{"msctls_statusbar32",0x4009,4,1},
        {"SysListView32",0x4008,4,1},{"SysHeader32",0x4008,4,0},
        {"SysTabControl32",0x400b,4,1},{"SysTreeView32",0x4008,4,1},
        {"msctls_trackbar32",0x4000,4,1},{"msctls_updown32",0x4003,4,1},
        {"msctls_progress32",0x4003,4,1},{"msctls_hotkey32",0x4000,24,0},
        {"SysAnimate32",0x4008,4,1},{"SysMonthCal32",0x4000,4,1},
        {"SysDateTimePick32",0x4000,4,1},{"ComboBoxEx32",0x4000,4,1},
        {"SysIPAddress32",0x400b,4,1},{"SysPager",0x4000,4,1},
        {"NativeFontCtl",0x4000,4,1}
    };
    U fields[12];
    for(U i=0;i<19;++i){
        for(U j=0;j<12;++j)fields[j]=0xa5a5a5a5;
        fields[0]=48;check(GetClassInfoExA(module,classes[i].name,fields),9112);
        check(fields[1]==classes[i].style&&fields[3]==0&&fields[4]==classes[i].extra,9113);
        check((fields[7]!=0)==classes[i].cursor,9114);
        check(fields[2]&&fields[6]==0&&fields[9]==0&&fields[11]==0,9115);
    }
    check(InitCommonControlsEx(info),9116); /* Original existing-class path. */
    U tree=CreateWindowExA(0,"SysTreeView32","",0,0,0,80,80,0,0,module,0);
    U list=CreateWindowExA(0,"SysListView32","",0,0,0,80,80,0,0,module,0);
    U status=CreateWindowExA(0,"msctls_statusbar32","",0,0,0,80,20,0,0,module,0);
    check(tree&&list&&status,9117);
    check(SendMessageA(tree,0x1105,0,0)==0&&SendMessageA(list,0x1004,0,0)==0,9118);
    check(SendMessageA(status,0x0401,0,(U)"original class registration"),9119);
    check(DestroyWindow(tree)&&DestroyWindow(list)&&DestroyWindow(status),9120);
    return 0;
}
#ifdef COMMON_INIT_STANDALONE
void mainCRTStartup(void){ExitProcess(run_common_init_test());}
#endif
