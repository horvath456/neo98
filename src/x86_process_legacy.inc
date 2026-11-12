// SPDX-License-Identifier: GPL-2.0-or-later
// Native providers for the implemented legacy startup adapters. Included
// after x86_process_children.inc to share its real process records unchanged.
#include "win98_process_legacy.h"
namespace X86 {
struct Application::LegacyProcessEnvironment {
 Application &app;ProcessLaunchEnvironment probe;
 bool path_locked=false;unsigned allocations=0,minimum_command=0;
 explicit LegacyProcessEnvironment(Application&a):app(a),probe{a}{}
 ~LegacyProcessEnvironment(){ReleaseMode();}
 // The original puts these structures on the stack of the caller, and the
 // stack pointer of a 32-bit caller is a flat address. Inside a 16-bit gate it
 // is an offset in a stack segment, so the block comes out of the flat arena
 // there instead; nothing of the original reads it back through SS.
 struct Scratch{LegacyProcessEnvironment&e;unsigned address,old;bool flat;
  Scratch(LegacyProcessEnvironment&env,unsigned n):e(env),old(e.app.Register(Reg::Esp)),flat(e.app.InWin16Gate()){
   if(flat){address=e.app.Allocate(n+32);return;}
   address=(old-n-32)&~15u;e.app.Register(Reg::Esp,address);}
  ~Scratch(){if(flat){if(address)e.app.Free(address);return;}e.app.Register(Reg::Esp,old);}
 };
 bool ProbeRead(unsigned p,unsigned &v){return probe.Read8(p,v);}
 bool ProbeWrite(unsigned p){unsigned value;return probe.Read8(p,value)&&probe.Write8(p,value);}
 unsigned Read8(unsigned p){if(p>=0xf1000000&&p<0xf1000044)return reinterpret_cast<const unsigned char*>(app.startup_info)[p-0xf1000000];unsigned char c=0;app.Read(p,&c,1);return c;}
 unsigned Read16(unsigned p){return Read8(p)|(Read8(p+1)<<8);}
 unsigned Read32(unsigned p){return Read16(p)|(Read16(p+2)<<16);}
 void Write8(unsigned p,unsigned v){unsigned char c=v;app.Store(p,&c,1);}
 void Write16(unsigned p,unsigned v){unsigned short w=v;app.Store(p,&w,2);}
 void Write32(unsigned p,unsigned v){app.Write32(p,v);}
 void Copy(unsigned to,unsigned from,unsigned bytes){unsigned char data[68];if(bytes!=68){app.last_error=87;return;}for(unsigned i=0;i<bytes;++i)data[i]=Read8(from+i);app.Store(to,data,bytes);}
 unsigned Startup(){return 0xf1000000;}
 void LockStartup(){app.CrtLock(true);}
 void UnlockStartup(){app.CrtLock(false);}
 unsigned LastError(){return app.last_error;}
 void ReleaseMode(){if(path_locked){path_locked=false;app.KernelPathLock(false,0);}}
 unsigned ConvertInput(unsigned,unsigned){app.last_error=50;return 0;}
 void FreeInput(unsigned,unsigned){}
 unsigned GetDirectory(unsigned n,unsigned out){unsigned a[]={n,out};return app.KernelPathApi("GetCurrentDirectoryA",a);}
 unsigned SetDirectory(unsigned name){return app.KernelPathApi("SetCurrentDirectoryA",&name);}
 unsigned Create(const unsigned*a){return app.ProcessApi("CreateProcessA",a);}
 unsigned Finish(unsigned kind,unsigned created,unsigned info){if(auto*s=app.ProcessFileState())app.threads[app.current_thread].file_api_flags=s->thread_flags;ReleaseMode();return Win98ProcessLegacy::Finish(*this,kind,created,info);}
 unsigned Allocate(unsigned bytes){if(++allocations>1&&bytes<minimum_command){app.last_error=87;return 0;}if(!bytes||bytes>32768){app.last_error=8;return 0;}return app.Allocate(bytes);}
 void Free(unsigned p){app.Free(p);}
 void Close(unsigned h){if((h&0xff000000)==0x40000000)app.FileApi("CloseHandle",&h);else app.ThreadApi("CloseHandle",&h);}
 unsigned Open(unsigned name){unsigned a[]={name,0x80000000,3,0,3,0x80,0};return app.FileApi("CreateFileA",a);}
 unsigned Search(unsigned name,unsigned out,unsigned capacity){
  char token[260];if(!probe.Text(name,token,sizeof token))return 0;
  const char*part=strrchr(token,'\\');part=part?part+1:token;
  if(!strchr(part,'.')){if(strlen(token)+4>=sizeof token){app.last_error=206;return 0;}strcat(token,".exe");}
  char path[260];unsigned node=0;
  auto candidate=[&](const char*prefix){char text[520];if(prefix&&*prefix)snprintf(text,sizeof text,"%s\\%s",prefix,token);else snprintf(text,sizeof text,"%s",token);return !app.files.FullPath(text,path)&&!app.files.store.Lookup(path,node)&&!(app.files.store.Get(node)->attributes&0x10);};
  bool found=false;if(strchr(token,'\\')||strchr(token,'/')||strchr(token,':'))found=candidate(nullptr);
  else{char directory[260];strcpy(directory,app.modules[0].path);char*slash=strrchr(directory,'\\');if(slash)*slash=0;found=candidate(directory)||candidate(nullptr)||candidate("C:\\WINDOWS\\SYSTEM")||candidate("C:\\WINDOWS");
   if(!found){unsigned env=app.FindProcessEnvironment("PATH");char entries[32768];if(env&&probe.Text(env,entries,sizeof entries)){char*p=entries;while(*p&&!found){char*end=strchr(p,';');if(end)*end=0;found=candidate(p);if(!end)break;p=end+1;}}}
  }
  if(!found){app.last_error=2;return 0;}unsigned length=strlen(path);if(length>=capacity)return length+1;app.Store(out,path,length+1);return length;
 }
 unsigned ThreadTask(unsigned tid){for(auto&c:children)if(c.used&&c.tid==tid)return c.tid&65535;app.last_error=6;return 0;}
 unsigned ReferenceProcess(unsigned h,unsigned,unsigned){auto*handle=app.FindKernelHandle(h);if(!handle||handle->kind!=6)return 0;app.ReferenceChild(handle->object);return handle->object+1;}
 unsigned ProcessFlags(unsigned){return 0;} // Native PE32 children have no Win16 process flag.
 void DereferenceProcess(unsigned token){app.ReleaseChild(token-1);}
 unsigned WaitThread(unsigned tid){
  // asks USER to wait at most 10000 ms. This provider observes an
  // actual blocked main-thread GetMessage, not the generic scheduler Idle.
  const auto deadline=Application::Now()+10000000ull;
  for(;;){ChildProcess*child=nullptr;for(auto&c:children)if(c.used&&c.tid==tid){child=&c;break;}
   if(!child||child->done||!child->app){app.last_error=6;return ~0u;}
   auto*target=child->app;
   // The child is idle once it has asked for its messages, with GetMessage
   // or PeekMessage, as WaitForInputIdle sees it (input_idle). The setup
   // engine of InstallShield 5 polls with PeekMessage and never waits in
   // GetMessage during its start, and its 16-bit starter waited out the
   // whole ten seconds.
   if(target->input_idle||(target->state==Idle&&target->threads[0].block==1&&target->threads[0].waiting))return 0;
   if(Application::Now()>=deadline)return 258;
   // The wait is USER's: the waiting task keeps its queue and answers sent
   // messages (USER), which Suspend does for block 16 as for
   // WaitForInputIdle. A plain scheduler yield did not, so a child
   // that sent to its parent during its start (the screen saver activating
   // itself against the window WinExec started it from) waited out the whole
   // ten seconds. It is a wait and not a loop of yields: the loop ran a
   // hundred thousand times a second.
   app.ProcessSentMessages();
   if(app.state==Failed||app.state==Exited)return ~0u;
   auto&t=app.threads[app.current_thread];t.block=16;t.deadline=Application::Now()+10000;app.Suspend(Application::Idle);
   if(app.state==Failed||app.state==Exited)return ~0u;
  }
 }
 bool SelectorExists(unsigned){return false;} // No Win16 task-selector objects on this PE32 path.
 unsigned SelectorAddress(unsigned){app.last_error=50;return 0;}
};
unsigned Application::LegacyProcessApi(ApiName name,const unsigned*a){
 LegacyProcessEnvironment e(*this);
 if(API_IS(name,"GetStartupInfoA")){
  if(!Win98ProcessLegacy::ProbeStartup(e,a[0])||!e.probe.Writable(a[0],68)){last_error=998;return 0;}
  Win98ProcessLegacy::Startup(e,a[0]);return 0;
 }
 const bool load=API_IS(name,"LoadModule");
 if(!(load?Win98ProcessLegacy::ProbeLoad(e,a[0],a[1]):Win98ProcessLegacy::ProbeString(e,a[0]))){last_error=998;return 0;}
 if(load){
  unsigned block[4];if(!e.probe.Span(a[1],block,16)){last_error=998;return 0;}
  // Bound the original signed length/allocation plus NUL concatenation.
  if(block[2]){unsigned short show[2];if(!e.probe.Span(block[2],show,4)){last_error=998;return 0;}
   if(show[0]==2&&block[1]){unsigned n;if(!e.ProbeRead(block[1],n)){last_error=998;return 0;}
    if(n>127){last_error=87;return 0;}
    if(n){char tail[128],file[260];if(!e.probe.Text(block[1]+1,tail,n+1)||!e.probe.Text(a[0],file,sizeof file))return 0;e.minimum_command=strlen(file)+strlen(tail)+2;}
   }
  }
  return Win98ProcessLegacy::LoadModule(e,a[0],a[1]);
 }
 KernelPathLock(true,0);e.path_locked=true;if(state==Failed||state==Exited)return 0;
 auto*mode=ProcessFileState();if(!mode)return 0;if(mode->process_flags&0x40){last_error=50;return 0;}
 mode->thread_flags=threads[current_thread].file_api_flags;
 return Win98ProcessLegacy::WinExec(e,*mode,a[0],a[1]);
}
}
