// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_shell_threads.h"
namespace X86 {
namespace { constexpr unsigned ShellThreadEntry=0x71000f10; }
// Per-process DLL TLS allocation; values live in the existing guest TEB/TLS.
// SHSetThreadRef borrows the pointer. Neither replacing nor TLS cleanup releases it.
struct Application::ShellThreadEnvironment {
 Application&app;unsigned reference_output=0;bool publish_reference=false;
 struct Slot{Application*owner=nullptr;unsigned tls=~0u;};
 static Slot*SlotFor(Application&a,bool create){static Slot slots[64];Slot*empty=nullptr;for(auto&s:slots){if(s.owner==&a)return &s;if(!s.owner&&!empty)empty=&s;}if(create&&empty){unsigned index=a.TlsApi("TlsAlloc",nullptr);if(index==~0u)return nullptr;empty->owner=&a;empty->tls=index;return empty;}return nullptr;}
 void Reference(unsigned p){if(publish_reference)app.Write32(reference_output,p);}
 unsigned TlsGet(){auto*s=SlotFor(app,true);return s?app.TlsApi("TlsGetValue",&s->tls):0;}
 void TlsSet(unsigned p){auto*s=SlotFor(app,true);if(s){unsigned a[]={s->tls,p};app.TlsApi("TlsSetValue",a);}else app.last_error=8;}
 unsigned Invoke(unsigned proc,unsigned param){if(app.state==Failed||app.state==Exited||app.threads[app.current_thread].done)return 0;return app.Call(proc,&param,1);}
 bool Alive(){return app.state!=Failed&&app.state!=Exited&&!app.threads[app.current_thread].done;}
 void AddRef(unsigned p){if(!Alive())return;Invoke(app.Read32(app.Read32(p)+4),p);}
 void Release(unsigned p){if(!Alive())return;Invoke(app.Read32(app.Read32(p)+8),p);}
 struct Scratch{Application&app;unsigned old,address;Scratch(Application&a,unsigned n):app(a),old(a.Register(Reg::Esp)),address((old-n-32)&~15u){app.Register(Reg::Esp,address);}~Scratch(){app.Register(Reg::Esp,old);}};
 unsigned Procedure(const char*dll,const char*name,bool load){Scratch space(app,160);app.Store(space.address,dll,strlen(dll)+1);app.Store(space.address+64,name,strlen(name)+1);unsigned module=app.ModuleApi("GetModuleHandleA",&space.address);if(!module&&load)module=app.ModuleApi("LoadLibraryA",&space.address);if(!module)return 0;unsigned a[]={module,space.address+64};return app.ModuleApi("GetProcAddress",a);}
 void ProcessRef(unsigned&p){p=0;unsigned proc=Procedure("SHELL32.DLL","SHGetInstanceExplorer",true);if(proc){Scratch out(app,4);app.Write32(out.address,0);app.Call(proc,&out.address,1);p=app.Read32(out.address);}}
 int ComInitialize(){unsigned proc=Procedure("OLE32.DLL","CoInitializeEx",true);if(!proc)return int(0x80004005u);unsigned a[]={0,6};int hr=int(app.Call(proc,a,2));if(hr<0){a[1]=4;hr=int(app.Call(proc,a,2));}return hr;}
 void ComUninitialize(){unsigned proc=Procedure("OLE32.DLL","CoUninitialize",true);if(proc)app.Call(proc,nullptr,0);}
 unsigned Event(){unsigned a[4]={};return app.ThreadApi("CreateEventA",a);}
 unsigned Thread(const Win98ShellThreads::StartData&data){
 // Original owns this record on the caller's stack until the worker signals.
 // Keep it below ESP through the wait, then restore it in Create's adapter.
 unsigned address=app.Register(Reg::Esp);if(!app.Store(address,&data,sizeof data))return 0;
 unsigned a[]={0,0,ShellThreadEntry,address,0,0};return app.CreateThread(a);
 }
 void Wait(unsigned h){unsigned a[]={h,~0u};app.ThreadApi("WaitForSingleObject",a);}
 void Signal(unsigned h){if(!Alive())return;app.ThreadApi("SetEvent",&h);}
 void Close(unsigned h){app.ThreadApi("CloseHandle",&h);}
};
unsigned Application::ShellThreadApi(ApiName name,const unsigned*a){
 ShellThreadEnvironment e{*this};
 if(API_IS(name,"SHSetThreadRef"))return Win98ShellThreads::SetRef(e,a[0]);
 if(API_IS(name,"SHGetThreadRef")){e.reference_output=a[0];e.publish_reference=true;unsigned p=0;return Win98ShellThreads::GetRef(e,p);}
 ShellThreadEnvironment::Scratch record(*this,sizeof(Win98ShellThreads::StartData));
 return Win98ShellThreads::Create(e,a[0],a[1],a[2],a[3]);
}
void Application::RunShellThread(){
 Win98ShellThreads::StartData data;unsigned address=Read32(Register(Reg::Esp)+4);
 if(!Read(address,&data,sizeof data))return;ShellThreadEnvironment e{*this};
 unsigned result=Win98ShellThreads::Worker(e,data);
 if(state!=Failed&&state!=Exited&&!threads[current_thread].done){threads[current_thread].result=result;threads[current_thread].done=true;}
}
void Application::FreeShellThreads(){auto*s=ShellThreadEnvironment::SlotFor(*this,false);if(s){TlsApi("TlsFree",&s->tls);*s=ShellThreadEnvironment::Slot{};}}
}
