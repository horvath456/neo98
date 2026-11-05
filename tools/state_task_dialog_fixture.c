// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;
#define API(ret,name,args) __declspec(dllimport) ret __stdcall name args
API(U,GetModuleHandleA,(const char*)); API(void,ExitProcess,(U));
API(U,CreateFileA,(const char*,U,U,void*,U,U,U)); API(U,WriteFile,(U,const void*,U,U*,void*));
API(U,SetFilePointer,(U,int,int*,U)); API(U,CloseHandle,(U));
API(U,RegisterClassA,(const void*)); API(U,CreateWindowExA,(U,const char*,const char*,U,int,int,int,int,U,U,U,void*));
API(U,DefWindowProcA,(U,U,U,U)); API(U,SetForegroundWindow,(U)); API(U,SetFocus,(U));
API(U,SetTimer,(U,U,U,void*)); API(U,SetWindowTextA,(U,const char*));
API(U,GetMessageA,(void*,U,U,U)); API(U,DispatchMessageA,(const void*));
API(U,EndTask,(U,U,U,U)); API(U,MessageBoxA,(U,const char*,const char*,U));
static U file, ticks, focuses;
#define CHECK(x) do { if (!(x)) ExitProcess(__LINE__); } while (0)
static U __stdcall Window(U hwnd,U message,U wp,U lp) {
    if (message==0x7) ++focuses;
    if (message==0x11) return 1;
    if (message==0x10) return 0;
    if (message==0x113) {
        ++ticks; U values[2]={ticks,focuses},written=0;
        CHECK(SetFilePointer(file,0,0,0)==0 && WriteFile(file,values,8,&written,0) && written==8);
        return 0;
    }
    return DefWindowProcA(hwnd,message,wp,lp);
}
void mainCRTStartup(void) {
    U instance=GetModuleHandleA(0);
    U klass[10]={0,(U)Window,0,0,instance,0,0,0,0,(U)"TaskStateOwner"};
    CHECK(RegisterClassA(klass));
    U owner=CreateWindowExA(0,"TaskStateOwner","Task state owner",0x10cf0000,20,20,300,180,0,0,instance,0);
    CHECK(owner);SetForegroundWindow(owner);SetFocus(owner);
    file=CreateFileA("D:\\PROGRESS.DAT",0xc0000000,3,0,2,0,0);CHECK(file!=~0u);
    CHECK(SetTimer(owner,1,100,0));
#ifdef STATE_ENDTASK
    U answer=EndTask(owner,0,0,0);
    CHECK(answer==2 || answer==4);
    CHECK(SetWindowTextA(owner,answer==2?"EndTask result 2":"EndTask result 4"));
    CHECK(MessageBoxA(owner,"The saved caller returned; its confirmation was normalized.","EndTask continuation complete",0));
    CHECK(CloseHandle(file));ExitProcess(0);
#else
    U message[7];while(GetMessageA(message,0,0,0))DispatchMessageA(message);
    CHECK(CloseHandle(file));ExitProcess(0);
#endif
}
