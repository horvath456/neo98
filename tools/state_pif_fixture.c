// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;
#define API(ret,name,args) __declspec(dllimport) ret __stdcall name args
API(U,GetModuleHandleA,(const char*)); API(void,ExitProcess,(U)); API(void,Sleep,(U));
API(U,CreateFileA,(const char*,U,U,void*,U,U,U)); API(U,WriteFile,(U,const void*,U,U*,void*));
API(U,SetFilePointer,(U,int,int*,U)); API(U,CloseHandle,(U));
API(U,RegisterClassA,(const void*)); API(U,CreateWindowExA,(U,const char*,const char*,U,int,int,int,int,U,U,U,void*));
API(U,DefWindowProcA,(U,U,U,U)); API(U,SetForegroundWindow,(U)); API(U,SetFocus,(U));
API(U,SetTimer,(U,U,U,void*)); API(U,SetWindowTextA,(U,const char*));
API(U,FindWindowExA,(U,U,const char*,const char*)); API(U,GetDlgItem,(U,U)); API(U,SendMessageA,(U,U,U,U));
API(U,GetMessageA,(void*,U,U,U)); API(U,PeekMessageA,(void*,U,U,U,U)); API(U,DispatchMessageA,(const void*));
static U file, ticks, prepared, initial_module, seen, last_sheet;
#define CHECK(x) do { if (!(x)) ExitProcess(__LINE__); } while (0)
static U __stdcall Window(U hwnd,U message,U wp,U lp) {
    if (message==0x10) { CHECK(CloseHandle(file));ExitProcess(0); }
    if (message==0x113) {
        ++ticks;
        U sheet_now=0;
            for (U top=FindWindowExA(0,0,0,0);top;top=FindWindowExA(0,top,0,0))
                for (U page=FindWindowExA(top,0,0,0);page;page=FindWindowExA(top,page,0,0)) {
                    U edit=GetDlgItem(page,4001);
                    if (edit) {
                        sheet_now=top;
                        if (!prepared) {
                            CHECK(SetWindowTextA(edit,"Saved PIF title"));
                            SendMessageA(page,0x111,4001u|(0x300u<<16),edit);prepared=1;
                        }
                    }
                }
        if (sheet_now && sheet_now!=last_sheet) { ++seen;last_sheet=sheet_now; }
        U values[4]={ticks,prepared,initial_module,GetModuleHandleA("COMCTL32.DLL")},written=0;
        CHECK(SetFilePointer(file,0,0,0)==0 && WriteFile(file,values,sizeof values,&written,0) && written==sizeof values);
        if (!sheet_now && seen>=2) { CHECK(CloseHandle(file));ExitProcess(0); }
        return 0;
    }
    return DefWindowProcA(hwnd,message,wp,lp);
}
void mainCRTStartup(void) {
    U instance=GetModuleHandleA(0);
    U klass[10]={0,(U)Window,0,0,instance,0,0,0,0,(U)"PifStateOwner"};CHECK(RegisterClassA(klass));
    U owner=CreateWindowExA(0,"PifStateOwner","PIF state owner",0x10cf0000,20,20,300,180,0,0,instance,0);
    CHECK(owner);SetForegroundWindow(owner);SetFocus(owner);initial_module=GetModuleHandleA("COMCTL32.DLL");
    file=CreateFileA("D:\\PROGRESS.DAT",0xc0000000,3,0,2,0,0);CHECK(file!=~0u);CHECK(SetTimer(owner,1,100,0));
    U message[7];
#ifdef STATE_PEEK
    for (;;) { if(PeekMessageA(message,0,0,0,1))DispatchMessageA(message);else Sleep(1); }
#else
    while(GetMessageA(message,0,0,0))DispatchMessageA(message);
#endif
    CHECK(CloseHandle(file));ExitProcess(0);
}
