// SPDX-License-Identifier: GPL-2.0-or-later
// Native class/procedure and guest-memory providers for original COMCTL setup.
#include "x86_app.h"
#include "win16_machine.h"
#include "win98_common_init.h"
#include <string.h>
namespace X86 {
unsigned Application::CommonInitApi(ApiName name,const unsigned *args) {
    namespace W=Win98CommonInit;
    if(API_IS(name,"InitCommonControls"))return W::Legacy(Register(Reg::Eax));
    struct Environment final:W::Environment {
        Application &app;
        bool module_attach=false;
        explicit Environment(Application &a):app(a){}
        bool Read(unsigned p,unsigned &v)override{
            // Private local INITCOMMONCONTROLSEX from the original attach
            // call site, never a public guest address alias.
            if(module_attach){if(p==1){v=8;return true;}if(p==5){v=0xff;return true;}return false;}
            return app.Read(p,&v,4);
        }
        bool Write(unsigned p,unsigned v)override{return app.Store(p,&v,4);}
        bool BadWrite(unsigned p,unsigned n)override{
            const unsigned a[]={p,n};return app.PointerProbeApi("IsBadWritePtr",a)!=0;
        }
        unsigned Exists(unsigned instance,unsigned index)override{
            unsigned fields[12]{};
            return app.GetNativeClassInfo(instance,W::Classes[index].name,fields);
        }
        unsigned LoadCursor(unsigned instance,unsigned resource)override{
            const unsigned a[]={instance,resource};return app.ImageApi("LoadCursorA",a);
        }
        unsigned Register(unsigned instance,unsigned index,unsigned cursor)override{
            const auto &c=W::Classes[index];
            // Existing native tree/list/status handlers retain their callable
            // identities. Every other control has its own explicit trap gate.
            const unsigned procedure=index==7?0x71000001:index==4?0x71000002:
                                     index==3?0x71000003:0x71000100+index;
            const unsigned fields[]={48,c.style,procedure,0,c.window_extra,instance,
                                     0,cursor,c.background,0,0,0};
            return app.RegisterNativeClass(fields,c.name);
        }
    } e(*this);
    if(API_IS(name,"DLL_PROCESS_ATTACH")){
        e.module_attach=true;return W::Initialize(e,1,args[0]);
    }
    if(API_IS(name,"DllGetVersion"))return W::Version(e,args[0]);
    if(API_IS(name,"InitCommonControlsEx")){
        unsigned instance=0;
        if(Win16Process()){
            // COMMCTRL registers these native classes for the task's module
            // database; a primary Win16 task does not load a flat COMCTL32.
            if(!win16.state||!Win16CommonControlModule())return 0;
            instance=win16.state->modules[0].handle;
        }else{
            const unsigned module=FindModule("COMCTL32.DLL");
            if(module>=ModuleCapacity){Fail("COMCTL32 initialization has no module instance");return 0;}
            instance=modules[module].handle;
        }
        return W::Initialize(e,args[0],instance);
    }
    Fail("Unknown common controls initialization export");return 0;
}
bool Application::UnsupportedCommonProcedure(unsigned procedure){
    if(procedure<0x71000100||procedure>=0x71000100+19)return false;
    char message[128];
    const char *prefix="COMCTL32 control procedure not ported: ";
    const char *name=Win98CommonInit::Classes[procedure-0x71000100].name;
    strcpy(message,prefix);strcat(message,name);Fail(message);return true;
}
}
