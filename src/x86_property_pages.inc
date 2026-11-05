// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_property_pages.h"
namespace X86 {
namespace {
// Native callback lifetime guard. Original raw handles have no reentrant
// double-destruction protection; refuse that case without changing their bytes.
struct PropertyPageGuard {const void*app;unsigned page;PropertyPageGuard*previous;static PropertyPageGuard*head;
 PropertyPageGuard(const void*a,unsigned p):app(a),page(p),previous(head){head=this;}
 ~PropertyPageGuard(){for(auto**p=&head;*p;p=&(*p)->previous)if(*p==this){*p=previous;break;}}
 static bool Busy(const void*a,unsigned p){for(auto*g=head;g;g=g->previous)if(g->app==a&&g->page==p)return true;return false;}
};
PropertyPageGuard*PropertyPageGuard::head=nullptr;
}
unsigned Application::PropertyPageApi(ApiName name,const unsigned*a)
{
    using U=unsigned;const bool resumed=API_IS(name,"ResumePropertyPage");
    auto&t=threads[current_thread];const U at=resumed?t.host_resume_at:t.host_frame_count;
    const bool destroy=resumed?t.host_frames[at].b!=0:API_IS(name,"DestroyPropertySheetPage");
    const U tail=t.tail;
    const bool allowed=!resumed&&(TakeFresh()||tail==TailRecord);
    struct E {
        Application&app;U at;bool kept;
        U Read(U p){return app.Read32(p);}void Write(U p,U v){app.Write32(p,v);}
        U Allocate(U n){U args[]={64,n};return app.Api("LocalAlloc",args);}
        void Free(U p){app.LocalMemoryApi("LocalFree",&p);}
        U Size(U p){return app.LocalMemoryApi("LocalSize",&p);}
        void Copy(U d,U s,U n){unsigned char b[4096];if(n>sizeof b){app.Fail("Oversized property page copy");return;}if(app.Read(s,b,n))app.Store(d,b,n);}
        U Clone(U p,U encoding){
            U bytes=0;for(;bytes<8192;bytes+=encoding==0?2:1){unsigned short c=0;if(!app.Read(p+bytes,&c,encoding==0?2:1))return 0;if(!c)break;}
            if(bytes>=8192){app.last_error=50;return 0;}
            if(encoding==1){U args[]={0,1,p,~0u,0,0};U count=app.NlsApi(594,args);if(!count)return 0;U out=Allocate(count*2);if(!out)return 0;args[4]=out;args[5]=count;if(!app.NlsApi(594,args)){Free(out);return 0;}return out;}
            bytes+=encoding==0?2:1;U out=Allocate(bytes);if(out){unsigned char b[8192];if(!app.Read(p,b,bytes)||!app.Store(out,b,bytes)){Free(out);return 0;}}return out;
        }
        void Reference(U p,int delta){U args[]={p};app.MemoryApi(delta>0?"InterlockedIncrement":"InterlockedDecrement",args);}
        U Callback(U proc,U reason,U parameter,U page){
            U args[]={0,reason,parameter};
            if(kept){app.threads[app.current_thread].host_frames[at].a=page;app.GiveTail(TailRecord);return app.Call(proc,args,3);}
            PropertyPageGuard guard(&app,page);return app.Call(proc,args,3);
        }
        bool LegacyRelease(U){app.last_error=50;return false;}
    }e{*this,at,false};
    U page=a[0];
    if(resumed){++t.host_resume_at;ResumeInner();
        U result=destroy?Win98PropertyPages::FinishDestroy(e,page):Read32(page-24);PopHostFrame();return result;}
    if(destroy){
        // Native heap membership replaces original unchecked handle dereferences.
        if(page<24||e.Size(page-24)<64){last_error=6;return 0;}
        page=Read32(page-8);if(page<24||e.Size(page-24)<64||PropertyPageGuard::Busy(this,page)){last_error=6;return 0;}
        for(const auto&thread:threads)for(U i=0;i<thread.host_frame_count;++i)
            if(thread.host_frames[i].kind==HostPropertyPage&&thread.host_frames[i].a==page){last_error=6;return 0;}
    }else{
        if(!virtual_memory.Access(page,8,false)){last_error=87;return 0;}
        U bytes=Read32(page);if(bytes<40||bytes>4096||(Read32(page+4)&0xffff0000u))return 0;
        if(!virtual_memory.Access(page,bytes,false)){last_error=87;return 0;}
        if(Read32(page+4)&0x8000){last_error=50;return 0;} // Original Win16 thunk page release provider is absent.
    }
    const U flags=Read32(page+4);
    if((flags&0x80)&&Read32(page+32)&&(destroy||Read32(page)>40)&&(!threads[current_thread].fiber||!threads[current_thread].fiber->Active())){last_error=50;return 0;}
    e.kept=allowed&&PushHostFrame(HostPropertyPage,page,destroy);
    const U result=destroy?Win98PropertyPages::Destroy(e,a[0]):Win98PropertyPages::Create(e,a[0],!API_IS(name,"CreatePropertySheetPageW"));
    if(e.kept)PopHostFrame();return result;
}
unsigned Application::ResumePropertyPage(unsigned page,bool){return PropertyPageApi("ResumePropertyPage",&page);}
}
