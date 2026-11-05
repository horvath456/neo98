// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;
#define API(ret,name,args) __declspec(dllimport) ret __stdcall name args
API(U,GetModuleHandleA,(const char*)); API(U,GetLastError,(void)); API(void,ExitProcess,(U));
API(U,GetDefaultCommConfigA,(const char*,void*,U*)); API(U,CommConfigDialogA,(const char*,U,void*));
API(U,RegisterClassA,(const void*));
API(U,CreateWindowExA,(U,const char*,const char*,U,int,int,int,int,U,U,U,void*));
API(U,DefWindowProcA,(U,U,U,U)); API(U,DestroyWindow,(U));
API(U,FindWindowA,(const char*,const char*)); API(U,FindWindowExA,(U,U,const char*,const char*));
API(U,GetDlgItem,(U,U)); API(U,SendMessageA,(U,U,U,U));
API(U,SetTimer,(U,U,U,void*)); API(U,KillTimer,(U,U));
API(U,MessageBoxA,(U,const char*,const char*,U));
static unsigned char config[52];
static U prepared;
#define CHECK(x) do { if (!(x)) ExitProcess(__LINE__); } while (0)
static U __stdcall Window(U hwnd,U message,U wp,U lp) {
    if (message==0x113 && !prepared) {
        U sheet=FindWindowA(0,"COM1 Properties"),page=FindWindowExA(sheet,0,0,"Port Settings");
        U combo=GetDlgItem(page,0x418),count=SendMessageA(combo,0x146,0,0);
        for (U i=0;i<count && i<20;++i) if (SendMessageA(combo,0x150,i,0)==115200) {
            SendMessageA(combo,0x14e,i,0);
            CHECK(SendMessageA(sheet,0x46e,0,0)); // PSM_APPLY fills the native DCB before capture.
            prepared=1;KillTimer(hwnd,1);break;
        }
        return 0;
    }
    return DefWindowProcA(hwnd,message,wp,lp);
}
void mainCRTStartup(void) {
    U instance=GetModuleHandleA(0),n=52;
    U klass[10]={0,(U)Window,0,0,instance,0,0,0,0,(U)"SerialStateOwner"};
    CHECK(RegisterClassA(klass));
    U owner=CreateWindowExA(0,"SerialStateOwner","Serial owner",0x10cf0000,10,10,220,100,0,0,instance,0);
    CHECK(owner && SetTimer(owner,1,200,0));
    CHECK(GetDefaultCommConfigA("COM1",config,&n));
    CHECK(CommConfigDialogA("COM1",owner,config)); // Apply followed by OK/Cancel still succeeds.
    CHECK(prepared && *(U*)(config+12)==115200);
    CHECK(!CommConfigDialogA("COM1",owner,config) && GetLastError()==1223);
    CHECK(*(U*)(config+12)==115200);
    CHECK(DestroyWindow(owner));
    CHECK(MessageBoxA(0,"DCB applied; cancellation preserved it; callers returned.","Serial continuation finished",0));
    ExitProcess(0);
}
