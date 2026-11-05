// SPDX-License-Identifier: GPL-2.0-or-later
#define mainCRTStartup EnumGuestMain
#include "serialui_enum_fixture.c"
#undef mainCRTStartup
#define API(ret,name,args) __declspec(dllimport) ret __stdcall name args
API(U,RegOpenKeyExA,(U,const char*,U,U,U*));
API(U,RegEnumKeyExA,(U,U,char*,U*,U*,char*,U*,void*));
API(U,RegQueryValueExA,(U,const char*,U*,U*,void*,U*));
API(U,RegSetValueExA,(U,const char*,U,U,const void*,U)); API(U,RegCloseKey,(U));
API(U,RegisterClassA,(const void*));
API(U,CreateWindowExA,(U,const char*,const char*,U,int,int,int,int,U,U,U,void*));
API(U,DefWindowProcA,(U,U,U,U)); API(U,FindWindowA,(const char*,const char*));
API(U,FindWindowExA,(U,U,const char*,const char*)); API(U,GetDlgItem,(U,U));
API(U,SendMessageA,(U,U,U,U)); API(U,SetTimer,(U,U,U,void*));
static U installed;
static void LocatePort(U key,U depth) {
    char port[32]={0};U size=sizeof port;
    if (!RegQueryValueExA(key,"PortName",0,0,port,&size) && port[0]=='C' && port[1]=='O' && port[2]=='M' && port[3]=='1' && !port[4]) {
        const char desc[]="Neo98 enum test";
        CHECK(!RegSetValueExA(key,"DeviceDesc",0,1,desc,sizeof desc));installed=1;return;
    }
    if (depth>=3) return;
    for(U index=0;!installed;++index) {
        char name[256];size=sizeof name;
        if (RegEnumKeyExA(key,index,name,&size,0,0,0,0)) break;
        U child=0;if (!RegOpenKeyExA(key,name,0,0x20019,&child)) {
            LocatePort(child,depth+1);CHECK(!RegCloseKey(child));
        }
    }
}
static U __stdcall Window(U hwnd,U message,U wp,U lp) {
    if (message==0x113) {
        U sheet=FindWindowA(0,"Enumerated COM1 Properties"),p=FindWindowExA(sheet,0,0,"Port Settings");
        U combo=GetDlgItem(p,0x418),count=SendMessageA(combo,0x146,0,0);
        for(U i=0;i<count&&i<20;++i)if(SendMessageA(combo,0x150,i,0)==115200){SendMessageA(combo,0x14e,i,0);break;}
        return 0;
    }
    return DefWindowProcA(hwnd,message,wp,lp);
}
void mainCRTStartup(void) {
    U root=0;CHECK(!RegOpenKeyExA(0x80000002u,"Enum",0,0x20019,&root));
    LocatePort(root,0);CHECK(!RegCloseKey(root)&&installed);
    U instance=GetModuleHandleA(0),klass[10]={0,(U)Window,0,0,instance,0,0,0,0,(U)"SerialEnumStateOwner"};
    CHECK(RegisterClassA(klass));
    U owner=CreateWindowExA(0,"SerialEnumStateOwner","Enum owner",0x10cf0000,10,10,220,100,0,0,instance,0);
    CHECK(owner&&SetTimer(owner,1,200,0));
    EnumGuestMain();
}
