// SPDX-License-Identifier: GPL-2.0-or-later
// Original KERNEL32 record and FS-chain dispatcher.
#pragma once
namespace Win98Exceptions {
using U=unsigned;
inline constexpr U InvalidDisposition=0xc0000026u,Noncontinuable=0xc0000025u,InvalidStack=0xc0000027u;
enum Result { Continue, Stop };
// TargetIp is unused in this Win98 implementation.
// Continue restores the caller context; TargetFrame itself is not unlinked.
template<class E> Result Unwind(E&e,U target,U record,U context){
 e.Write(record+4,e.Read(record+4)|(target?2u:6u));
 U frame=e.Head();
 while(frame!=~0u&&e.Alive()){
  if(!e.Iteration())return Stop;
  bool alternate=false;
  if(e.Read(record)!=InvalidStack&&!(frame<=e.StackTop()&&frame>=e.StackBottom())){
   if(!e.AlternateStack(frame)){e.Raise(InvalidStack,record,context);break;}alternate=true;
  }
  if(frame==target)return Continue;
  if(!e.FrameReadable(frame))return Stop;
  U handler=e.Read(frame+4);
  if(!e.SystemHandlersOnly()||(handler>=0x80000000u&&handler<=0xbfffffffu)){
   U dispatcher=0,disposition=e.Handler(handler,record,frame,context,dispatcher);
   if(!e.Alive())return Stop;
   if(disposition==3)frame=dispatcher;
   else if(disposition!=1)e.Raise(InvalidDisposition,record,context);
  }
  if(!e.Alive()||!e.FrameReadable(frame))return Stop;
  U next=e.Read(frame);
  if(next<=frame&&!alternate){e.Raise(InvalidStack,record,context);break;}
  e.WriteHead(next);frame=next;
 }
 if(!e.Alive())return Stop;
 if(target==~0u)return Continue;
 e.Reraise(record,context);return Stop;
}
// Negative counts take the original signed branch and then
// REP MOVSD with a huge unsigned count; the native readable-memory provider
// rejects that unsafe input before this core is entered.
template<class E> void Record(E&e,U out,U code,U flags,U count,U arguments){
 e.Write(out,code);e.Write(out+4,flags&1);e.Write(out+8,0);e.Write(out+12,0);e.Write(out+16,0);
 if(arguments){if(count<0x80000000u&&count>15)count=15;e.Write(out+16,count);e.Copy(out+20,arguments,count*4);}
}
// Native storage maps the process slot; no thread-local filter copy.
template<class E> U SetFilter(E&e,U slot,U value){U old=e.Read(slot);e.Write(slot,value);return old;}
// guard handler used around calls into application frame handlers.
template<class E> U Nested(E&e,U record,U frame,U dispatcher){
 if(e.Read(record+4)&6)return 1;e.Write(dispatcher,e.Read(frame+8));return 2;
}
// The remaining original error-dialog/debugger-launch tail is
// an explicit provider; it cannot manufacture a handler disposition.
template<class E> U Unhandled(E&e,U pointers){
 U record=e.Read(pointers);
 if(e.Read(record)==0xc0000005u&&e.Read(record+20)&&e.RepairWriteFault(e.Read(record+24)))return ~0u;
 if(e.Debugged()||e.ThreadFaultReported())return 0;
 U result=0,filter=e.Filter();if(filter){result=e.FilterCall(filter,pointers);if(result==1||result==~0u)return result;}
 if(e.SuppressFaultBox())return 1;
 return e.DefaultFilter(pointers,result);
}
// installs a scope whose filter Windows 98 calls UEF. The shared
// _except_handler3 resumes for a negative filter result, searches
// for zero, and unwinds/terminates for a positive one. This frame precedes
// Dispatch's last-chance path, which deliberately ignores the UEF result.
template<class E> U ThreadStart(E&e,U record,U frame,U context){
 if(e.Read(record+4)&6)return 1;
 U result=e.Unhandled(record,context);
 if(result&0x80000000u)return 0;
 if(result)e.ThreadTerminate(record,frame,context);
 return 1;
}
// Stack bounds and the legacy out-of-stack provider are kept
// separate. Dispatch returns only after the original continuation/termination
// provider boundary; ordinary handlers still execute and choose disposition.
template<class E> Result Dispatch(E&e,U context,U record,U trap){
 if(trap==1)e.Write(context+0xc0,e.Read(context+0xc0)&~0x100u);
 if(e.Debug(true,trap,record,context))return Continue;
 if(e.KernelDebug(trap)){if(trap==1||trap==3)return e.VmEnd(trap,context);e.ReportThreadFault();}
 U frame=e.Head(),nested=0;
 while(frame!=~0u&&e.Alive()){
  if(!e.Iteration())return Stop;
  bool alternate=false;
  if(e.Read(record)!=InvalidStack&&!(frame<=e.StackTop()&&frame>=e.StackBottom())){
   if(!e.AlternateStack(frame)){e.Raise(InvalidStack,record,context);break;}alternate=true;
  }
  if(!e.FrameReadable(frame))return Stop;
  U handler=e.Read(frame+4);
  if(!e.SystemHandlersOnly()||(handler>=0x80000000u&&handler<=0xbfffffffu)){
   U dispatch_frame=0;U disposition=e.Handler(handler,record,frame,context,dispatch_frame);
   if(!e.Alive())return Stop;
   if(nested==frame){e.Write(record+4,e.Read(record+4)&~16u);nested=0;}
   if(disposition==0){if(e.Read(record+4)&1)e.Raise(Noncontinuable,record,context);else return Continue;}
   else if(disposition==2){e.Write(record+4,e.Read(record+4)|16u);if(nested<dispatch_frame)nested=dispatch_frame;}
   else if(disposition!=1)e.Raise(InvalidDisposition,record,context);
  }
  if(!e.Alive())return Stop;
  U next=e.Read(frame);if(next<=frame&&!alternate){e.Raise(InvalidStack,record,context);break;}frame=next;
 }
 if(!e.Alive())return Stop;
 e.Unhandled(record,context); // Original deliberately discards EAX.
 if(!e.Alive())return Stop;
 if(e.Debug(false,trap,record,context))return Continue;
 if(!e.ThreadFaultReported())return e.Terminate(record);
 e.ClearThreadFault();return e.VmEnd(trap,context);
}
}
