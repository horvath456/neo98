// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_exceptions.h"
namespace X86 {
namespace {
// NEO98-private kernel scratch. TIB+30h points at the shared process record;
// this filter slot is private to the native exception provider.
constexpr unsigned ExceptionFilterSlot=Application::ProcessPage+0xf00,ExceptionGuardHandler=0x71000e00;
struct GuestExceptionContext {unsigned d[51]{};};
constexpr unsigned ContextFlags=0x10007;
constexpr int ContextRegisters[]={Reg::Gs,Reg::Fs,Reg::Es,Reg::Ds,
 Reg::Edi,Reg::Esi,Reg::Ebx,Reg::Edx,Reg::Ecx,Reg::Eax,
 Reg::Ebp,Reg::Eip,Reg::Cs,Reg::Eflags,Reg::Esp,Reg::Ss};
}
struct Application::ExceptionEnvironment {
 Application&app;unsigned nesting=0,iterations=0;bool unwinding=false,abandoned=false;
 unsigned Read(unsigned p){return app.Read32(p);}void Write(unsigned p,unsigned v){app.Write32(p,v);}
 void Copy(unsigned d,unsigned s,unsigned n){unsigned char b[60];if(n>sizeof b){app.Fail("Exception arguments exceed native readable provider");return;}if(app.Read(s,b,n))app.Store(d,b,n);}
 // An abandoned handler frame ends the dispatch. The original never comes
 // back from such a handler either, so there is no disposition to read and
 // no verdict to reach; the walk stops where it stands. A thread
 // that ended inside a handler, because its routine returned there, has no
 // walk left either.
 bool Alive(){return !abandoned&&app.state!=Failed&&app.state!=Exited&&!app.threads[app.current_thread].done;}
 bool Iteration(){if(++iterations<=4096)return true;app.Fail("SEH frame provider iteration bound exceeded");return false;}
 void WriteHead(unsigned value){Write(app.Teb(),value);}
 unsigned Head(){return Read(app.Teb());}unsigned StackTop(){return Read(app.Teb()+4);}unsigned StackBottom(){return Read(app.Teb()+8);}
 bool FrameReadable(unsigned p){if(app.virtual_memory.Access(p,8,false))return true;app.Fail("SEH frame is not readable guest memory");return false;}
 bool AlternateStack(unsigned){return false;}bool SystemHandlersOnly(){return false;}
 bool Debug(bool,unsigned,unsigned,unsigned){return false;}bool KernelDebug(unsigned){return false;}
 bool Debugged(){return false;}bool ThreadFaultReported(){return false;}
 void ReportThreadFault(){}void ClearThreadFault(){}
 bool RepairWriteFault(unsigned address){return app.RepairResourceWrite(address);}bool SuppressFaultBox(){return app.process_error_mode&2;}
 unsigned Filter(){return Read(ExceptionFilterSlot);}
 unsigned FilterCall(unsigned proc,unsigned pointers){return app.Call(proc,&pointers,1);}
 // The first exception parameter is named because the original MSVC delay-load
 // helper passes its DelayLoadInfo there; naming it keeps the stopped guest
 // readable without a second inspection path. Nothing here is modified.
 // The error box of Win98 where a runner provides it (src/x86_fault_box.inc):
 // after Close the filter answers EXCEPTION_EXECUTE_HANDLER, and the caller
 // ends the process.
 unsigned DefaultFilter(unsigned pointers,unsigned){unsigned record=Read(pointers),count=Read(record+16);
  if(app.FaultBox(record,Read(pointers+4)))return 1;
  if(app.state==Application::Failed||app.state==Application::Exited)return 0;
  char message[200];snprintf(message,sizeof message,"Unhandled exception %08x at %08x parameters %u first %08x requires Win98 debugger/error-dialog provider",Read(record),Read(record+12),count,count?Read(record+20):0);app.Fail(message);return 0;}
 Win98Exceptions::Result Terminate(unsigned record){return TerminateCode(record,Read(record));}
 Win98Exceptions::Result TerminateCode(unsigned record,unsigned exit_code){char message[128];snprintf(message,sizeof message,"Unhandled guest exception %08x at %08x",Read(record),Read(record+12));
  // With the error box provided the process ends alone, with or without
  // the box (SEM_NOGPFAULTERRORBOX, a filter of its own); the host still
  // gets its report.
  if(Application::fault_box&&app.user_initialized){if(app.log)app.log(message);if(!app.fault_notified)app.NotifyCrash(message);app.fault_notified=false;app.TerminateSelf(exit_code);return Win98Exceptions::Stop;}
  app.Fail(message);return Win98Exceptions::Stop;}
 Win98Exceptions::Result VmEnd(unsigned,unsigned){app.Fail("SEH VM-end provider is unavailable");return Win98Exceptions::Stop;}
 unsigned Handler(unsigned proc,unsigned record,unsigned frame,unsigned context,unsigned&dispatcher){
  if(proc==Application::ThreadStartHandler)return Win98Exceptions::ThreadStart(*this,record,frame,context);
  // KERNEL32, the FT_Prolog frame handler: unwind flags continue the
  // search; otherwise the thunk resumes its caller with the error result.
  if(proc==Application::FtThunkHandler){if(unwinding||(Read(record+4)&6))return 1;return app.FtThunkFault(frame,context)?0:1;}
  if(proc==ExceptionGuardHandler+16){if(!(Read(record+4)&6))return 1;dispatcher=Read(frame+8);return 3;}
  if(proc==ExceptionGuardHandler){unsigned lease=app.Allocate(4);if(!lease){app.Fail("SEH dispatcher allocation failed");return 1;}Write(lease,0);unsigned result=Win98Exceptions::Nested(*this,record,frame,lease);dispatcher=Read(lease);app.Free(lease);return result;}
  // Match 's linked guard and cdecl four-argument handler call.
  const unsigned sp=app.Register(Reg::Esp),guard=sp-12,previous=Head();
  if(guard<StackBottom()||!app.virtual_memory.Access(guard,12,true)){app.Fail("SEH guard exceeds guest stack");return 1;}
  unsigned lease=app.Allocate(4);if(!lease){app.Fail("SEH dispatcher allocation failed");return 1;}Write(lease,0);
  Write(guard,previous);Write(guard+4,ExceptionGuardHandler+(unwinding?16:0));Write(guard+8,frame);Write(app.Teb(),guard);
  unsigned args[]={record,frame,context,lease};bool left=false;
  // A handler of the search may leave its frame behind, whether a fault or
  // RaiseException brought the guest here: it unwinds and jumps into its own
  // code, and the kernel frames below it stay on the stack it jumped over.
  // The frame ends at the first block above its mark,
  // and Dispatch does not finish the RaiseException gate below it;
  // ExceptionApi restores no context once the walk stopped. A frame left open
  // would hold a callback level until an outer mark or the thread end closes
  // it. The handlers that RtlUnwind calls come back to the unwind and keep
  // the plain callback frame.
  app.ExceptionState().calling_exception_handler=!unwinding;
  unsigned result=app.Call(proc,args,4,nullptr,&left);
  // unlinks its guard only when the handler returns. A handler that
  // left leaves the chain to the code it continued in, which has unwound it
  // already (RtlUnwind) or restored its own frame; the head the guard saved
  // would put the removed frames back on the chain.
  // A nested handler can leave while this one still returns normally. Use
  // the outcome of this Call, not the process-wide count of abandoned calls
  // or ESP, which can equal its old value after a non-local jump.
  if(!left)Write(app.Teb(),Read(guard));dispatcher=Read(lease);app.Free(lease);
  // The handler did not return: it unwound and continued its own path, so
  // there is nothing left to search and nothing left to resume. The original
  // never comes back here either; its dispatcher frame stays on the stack the
  // handler jumped over. A disposition would be read as a verdict, and
  // a zero would even raise STATUS_NONCONTINUABLE_EXCEPTION on a record that
  // carries EXCEPTION_NONCONTINUABLE, so the walk is stopped instead.
  if(left){abandoned=true;return 1;}
  return result;
 }
 unsigned Unhandled(unsigned record,unsigned context){unsigned lease=app.Allocate(8);if(!lease){app.Fail("SEH exception-pointer allocation failed");return 0;}Write(lease,record);Write(lease+4,context);unsigned result=Win98Exceptions::Unhandled(*this,lease);app.Free(lease);return result;}
 void ThreadTerminate(unsigned record,unsigned frame,unsigned){
  // _global_unwind2 calls RtlUnwind(frame, return_ip, 0, 0).
  // Reuse that provider so cleanup handlers see its generated record and
  // fresh context (EAX=0), rather than the original fault's record/context.
  unsigned proc=0;if(!app.NativeProcedure("KERNEL32.DLL","RtlUnwind",0,&proc,false)||!proc){app.Fail("Thread-start unwind provider is unavailable");return;}
  unsigned args[]={frame,0,0,0};bool left=false;app.Call(proc,args,4,nullptr,&left);
  if(left)abandoned=true;
  if(!Alive())return;
  TerminateCode(record,~0u); // terminate the process with -1.
 }
 void Reraise(unsigned record,unsigned context){const bool saved=unwinding;unwinding=false;Win98Exceptions::Dispatch(*this,context,record,~0u);unwinding=saved;}
 void Raise(unsigned code,unsigned previous,unsigned context){
  if(++nesting>16){app.Fail("Nested SEH provider depth exceeded");--nesting;return;}
  unsigned record=app.Allocate(288);if(!record){app.Fail("Nested SEH record allocation failed");--nesting;return;}
  Win98Exceptions::Record(*this,record,code,1,0,0);Write(record+8,previous);Write(record+12,Read(context+0xb8));
  GuestExceptionContext nested_context;if(app.Read(context,&nested_context,sizeof nested_context)){app.Store(record+80,&nested_context,sizeof nested_context);Reraise(record,record+80);}app.Free(record);--nesting;
 }
};
unsigned Application::ExceptionApi(ApiName name,const unsigned*a)
{
 ExceptionEnvironment e{*this};
 if(API_IS(name,"SetUnhandledExceptionFilter"))return Win98Exceptions::SetFilter(e,ExceptionFilterSlot,a[0]);
 if(!threads[current_thread].fiber||!threads[current_thread].fiber->Active()){last_error=50;return 0;}
 if(API_IS(name,"UnhandledExceptionFilter"))return Win98Exceptions::Unhandled(e,a[0]);
 const unsigned sp=Register(Reg::Esp),ret=Read32(sp);
 const bool unwind=API_IS(name,"RtlUnwind"),breakpoint=API_IS(name,"DebugBreak");e.unwinding=unwind;
 if(unwind&&a[2]&&!virtual_memory.Access(a[2],80,true)){Fail("RtlUnwind record is inaccessible guest memory");return 0;}
 // The original signed count branch can request billions of DWORD reads.
 // Refuse that input as a provider failure before constructing a record.
 if(!unwind&&a[3]&&a[2]>=0x80000000u){Fail("RaiseException negative count exceeds native argument provider");return 0;}
 if(!unwind&&a[3]&&a[2]&&((unsigned long long)a[2]*4>0xffffffffull||!virtual_memory.Access(a[3],a[2]*4,false))){Fail("RaiseException argument probe violates guest memory");return 0;}
 unsigned lease=Allocate(288);if(!lease){Fail("SEH context allocation failed");return 0;}
 const unsigned context=lease+80;GuestExceptionContext captured;captured.d[0]=ContextFlags;
 for(unsigned i=0;i<16;++i)captured.d[35+i]=Register(ContextRegisters[i]);
 // DebugBreak is an INT 3 with no arguments: the record carries
 // code 80000003 at the import gate and the continuation is the caller.
 captured.d[46]=ret;captured.d[49]=sp+(breakpoint?4:20);
 if(unwind)captured.d[44]=a[3];
 Store(context,&captured,sizeof captured);
 const unsigned record=unwind&&a[2]?a[2]:lease;
 if(!unwind||!a[2]){Win98Exceptions::Record(e,record,unwind?Win98Exceptions::InvalidDisposition:breakpoint?0x80000003u:a[0],unwind||breakpoint?0:a[1],unwind||breakpoint?0:a[2],unwind||breakpoint?0:a[3]);e.Write(record+12,breakpoint?pending:ret);}
 const auto result=!e.Alive()?Win98Exceptions::Stop:unwind?Win98Exceptions::Unwind(e,a[0],record,context):Win98Exceptions::Dispatch(e,context,record,~0u);
 if(result==Win98Exceptions::Continue&&e.Alive()){
  GuestExceptionContext continued;if(Read(context,&continued,sizeof continued)){
   // This provider captures/restores integer, control and segment state. The
   // guest's x87/debug/extended context needs a separate VWIN32 provider.
   if((continued.d[0]&~ContextFlags)||!(continued.d[0]&0x10000)){Fail("SEH context requests unported register groups");}
   else if(!virtual_memory.Access(continued.d[46],1,false)||!virtual_memory.Access(continued.d[49],1,false))Fail("SEH continuation targets inaccessible guest memory");
   else for(unsigned i=0;i<16;++i){unsigned group=i<4?4:(i<10?2:1);if(continued.d[0]&group)Register(ContextRegisters[i],continued.d[35+i]);}
  }
 }
 Free(lease);return 0;
}
void Application::InstallThreadStartHandler(unsigned teb,unsigned frame)
{
 // installs an EH3 registration: previous, handler, scope, level.
 // Windows 98 selects scope 0 before entering user code. Indeo's _setjmp3
 // reads frame+12, and CRT local unwind reads the scope table at frame+8.
 // This scope is an except clause, not a finally clause; its nonzero filter
 // is handled by the native ThreadStart projection rather than guest code.
 Write32(frame,Read32(teb));Write32(frame+4,ThreadStartHandler);
 Write32(frame+8,frame+16);Write32(frame+12,0);
 Write32(frame+16,~0u);Write32(frame+20,ThreadStartHandler);Write32(frame+24,ThreadStartHandler);
 Write32(teb,frame);
}
// The guard handler as a function. The dispatch puts a guard frame of
// around every handler it calls; its handler is the token
// ExceptionGuardHandler (+16 while unwinding), which only the dispatch above
// reads. An original unwind loop walks fs:[0] and calls each frame's handler
// itself: RPCRT4  (local unwind of RpcRaiseException under OLE32)
// reached a guard that a handler further up had left in the chain and
// fetched from the token (Explorer, My Computer opened a second time while
// its window was minimized). The fetch becomes the cdecl call
// KERNEL32 answers there: record, frame, context and the dispatcher context
// on the stack, the disposition in EAX, back to the caller.
bool Application::EnterExceptionGuard()
{
 if(!memory_fault.valid||(memory_fault.type!=CpuFault::FetchUnmapped&&memory_fault.type!=CpuFault::FetchProtected))return false;
 const unsigned token=Register(Reg::Eip);
 if(token!=memory_fault.address||(token!=ExceptionGuardHandler&&token!=ExceptionGuardHandler+16&&token!=ThreadStartHandler)||Win16Code())return false;
 const unsigned esp=Register(Reg::Esp);unsigned args[5]{};
 if(!Read(esp,args,sizeof args))return false;
 const unsigned record=args[1],frame=args[2],dispatcher=args[4];
 ExceptionEnvironment e{*this};unsigned result=1;
 if(token==ThreadStartHandler){result=Win98Exceptions::ThreadStart(e,record,frame,args[3]);if(!e.Alive()){memory_fault={};return true;}}
 else if(token==ExceptionGuardHandler+16){if(Read32(record+4)&6){if(dispatcher)Write32(dispatcher,Read32(frame+8));result=3;}}
 else if(dispatcher)result=Win98Exceptions::Nested(e,record,frame,dispatcher);
 else result=(Read32(record+4)&6)?1:2;
 Register(Reg::Eax,result);Register(Reg::Esp,esp+4);Register(Reg::Eip,args[0]);
 memory_fault={};return true;
}
}
