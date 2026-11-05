// SPDX-License-Identifier: GPL-2.0-or-later
#include "x86_app.h"
#include "win98_multiwait.h"
#include "win98_queue_status.h"
#include <string.h>
namespace X86 {
namespace {
// A thread of another process, named by a key of its own so the wait keys stay
// 32 bits. DuplicateHandle carries a thread across processes in Windows 98
// (KERNEL32 refuses only the two memory-context types and type 15h),
// and DirectSound waits in the client process on the mixer thread it asked
// DDHELP.EXE to create. The thread table is the one kernel table of this port
// that belongs to a process, so a waitable reference to a foreign thread needs
// the process with it; this table holds the pairs for the length of a wait.
//
constexpr unsigned ForeignThreadKind = 11;
struct ForeignThread
{
    Application *owner = nullptr;
    unsigned index = 0, uses = 0;
};
ForeignThread foreign_threads[64];
unsigned HoldForeignThread(Application *owner, unsigned index)
{
    for (auto &entry : foreign_threads)
        if (entry.uses && entry.owner == owner && entry.index == index)
        {
            ++entry.uses;
            return unsigned(&entry - foreign_threads) + 1;
        }
    for (auto &entry : foreign_threads)
        if (!entry.uses)
        {
            entry = ForeignThread{owner, index, 1};
            return unsigned(&entry - foreign_threads) + 1;
        }
    return 0;
}
bool ForeignThreadDone(unsigned slot)
{
    const auto &entry = foreign_threads[slot];
    return !entry.owner || entry.owner->ThreadDone(entry.index);
}
} // namespace
bool Application::ThreadDone(unsigned index) const { return index < ThreadCapacity && threads[index].done; }
unsigned Application::WaitQueueStatus(unsigned index,bool observe,unsigned mask){
 unsigned status=User::WaitQueueStatus(observe,mask);
 status|=SentQueueStatus(index,observe&&(mask&0x40));
 const auto now=Now();
 for(auto&timer:timers)if(!timer.caret&&timer.thread==index&&(!timer.hwnd||User::IsWindow(timer.hwnd))){
  bool ready=timer.pending||(timer.used&&now>=timer.deadline);
  if(ready){status|=0x100000;if(!timer.wait_observed)status|=0x10;if(observe&&(mask&0x10))timer.wait_observed=true;}
  else timer.wait_observed=false;
 }
 return status;
}
unsigned Application::QueueStatusApi(ApiName name,const unsigned*a){
 const bool input=API_IS(name,"GetInputState");
 const unsigned mask=input?Win98QueueStatus::InputMask:Win98QueueStatus::Mask(a[0]);
 // USER synchronizes input, but querying queue bits does not dispatch sent WndProcs.
 DrainInput();
 const unsigned status=WaitQueueStatus(current_thread,true,mask);
 unsigned changed=status&65535;
 return Win98QueueStatus::Query(status>>16,changed,mask,input);
}
bool Application::MultiwaitReady(unsigned index){
 auto&t=threads[index];auto&w=t.multiwait;
 if(w.message&&index==InputThread()&&InputPending())return true; // Drain real input before retesting its queue category.
 unsigned status=w.message?WaitQueueStatus(index):0;
 bool queue=w.message&&((status|((w.flags&4)?status>>16:0))&w.mask&65535);
 bool all=true;
 for(unsigned i=0;i<w.count;++i){
  unsigned key=w.objects[i],kind=key>>16,object=(key&65535)-1;
  bool ready=kind==ForeignThreadKind?ForeignThreadDone(object):
   kind==1?events[object].signaled:kind==2?threads[object].done:
   kind==3?(!kernel_mutexes[object].owner||kernel_mutexes[object].owner==t.id):
   kind==4?Win98Semaphore::CanAcquire(semaphores[object].data):
   kind==8?change_notifications[object].signaled:
   kind==9?ForeignProcessSignaled(object):ChildSignaled(object,kind==7);
  if(ready&&!w.all)return true;if(!ready)all=false;
 }
 return (w.all?all&&(!w.message||queue):queue)||(t.deadline&&Now()>=t.deadline);
}
unsigned Application::AcquireWaitObject(unsigned key,unsigned tid){
 unsigned kind=key>>16,object=(key&65535)-1;
 if(kind==1&&!events[object].manual)events[object].signaled=false;
 if(kind==4)Win98Semaphore::Acquire(semaphores[object].data);
 if(kind==3){auto&m=kernel_mutexes[object];bool abandoned=m.abandoned;m.owner=tid;++m.recursion;m.abandoned=false;return abandoned?0x80:0;}
 return 0;
}
struct Application::MultiwaitEnvironment {
 Application&app;bool message=false;unsigned mask=0,flags=0;
 explicit MultiwaitEnvironment(Application&a):app(a){}
 void Error(unsigned e){app.last_error=e;}
 void Lock(){}void Unlock(){} // Cooperative scheduler owns the native critical section.
 void DeliverAPC(){app.DeliverApcs();}
 unsigned Resolve(unsigned handle){
  app.TickWaitableTimers();
  auto*h=app.FindKernelHandle(handle);
  // Required access 0, as WaitForMultipleObjectsEx looks handles up; a missing SYNCHRONIZE is no error on Win98.
  if(!h||h->kind==5||h->kind>=10){Error(6);return 0;}
  unsigned k=h->kind,o=h->object;
  // A thread handle that came from another process names that process's
  // thread, so the key names the pair instead of an index of this one.
  if(k==2){
   Application*owner=app.ThreadHandleOwner(h);
   if(owner!=&app){
    const unsigned slot=HoldForeignThread(owner,o);
    if(!slot){Error(8);return 0;}
    ++owner->threads[o].references;
    return(ForeignThreadKind<<16)|slot;
   }
  }
  if(k==1)++app.events[o].references;else if(k==2)++app.threads[o].references;
  else if(k==3)++app.kernel_mutexes[o].references;else if(k==4)++app.semaphores[o].references;
  else if(k==8)++app.change_notifications[o].references;
  // A foreign process handle owns no record of this session; it names a
  // process identity and needs no reference of its own.
  else if(k!=9)app.ReferenceChild(o);
  return(k<<16)|(o+1);
 }
 void Release(unsigned key){
  unsigned k=key>>16,o=(key&65535)-1;
  if(k==ForeignThreadKind){
   auto&entry=foreign_threads[o];
   if(entry.owner)--entry.owner->threads[entry.index].references;
   if(entry.uses&&!--entry.uses)entry=ForeignThread{};
   return;
  }
  if(k==1){if(!--app.events[o].references&&app.events[o].timer)app.CancelTimerApcs(o);}else if(k==2)--app.threads[o].references;
  else if(k==3){if(!--app.kernel_mutexes[o].references)app.kernel_mutexes[o]={};}
  else if(k==4){if(!--app.semaphores[o].references)app.semaphores[o]={};}
  else if(k==8){if(!--app.change_notifications[o].references)app.CloseChangeNotification(o+1);}
  else if(k!=9)app.ReleaseChild(o);
 }
 unsigned Wait(unsigned count,const unsigned*objects,unsigned timeout,unsigned all,unsigned alert){

  auto&t=app.threads[app.current_thread];auto saved=t.multiwait;
  const bool saved_alert=t.alertable;t.alertable=alert!=0;
  t.multiwait={};auto&w=t.multiwait;w.count=count;w.all=all!=0;w.message=message;w.mask=mask;w.flags=flags;
  for(unsigned i=0;i<count;++i)w.objects[i]=objects[i];
  const auto deadline=timeout==~0u?0:app.Now()+(unsigned long long)timeout*1000;
  unsigned result=~0u;
  for(;;){
   if(message)app.DrainInput();
   if(app.state==Failed||app.state==Exited)break;
   if(t.alertable&&t.apc_count){result=0xc0;break;}
   unsigned status=message?app.WaitQueueStatus(app.current_thread):0;
   bool queue=message&&((status|((flags&4)?status>>16:0))&mask&65535),ready=true;
   unsigned first=~0u;
   for(unsigned i=0;i<count;++i){
    unsigned key=objects[i],k=key>>16,o=(key&65535)-1;
    bool yes=k==ForeignThreadKind?ForeignThreadDone(o):
     k==1?(app.events[o].signaled||(t.pulse_handle&&app.FindKernelHandle(t.pulse_handle)&&app.FindKernelHandle(t.pulse_handle)->object==o)):k==2?app.threads[o].done:k==3?(!app.kernel_mutexes[o].owner||app.kernel_mutexes[o].owner==t.id):k==4?Win98Semaphore::CanAcquire(app.semaphores[o].data):k==8?app.change_notifications[o].signaled:k==9?Application::ForeignProcessSignaled(o):app.ChildSignaled(o,k==7);
    if(!yes)ready=false;else if(first==~0u)first=i;
   }
   bool acquired=all?(ready&&(!message||queue)):(first!=~0u||queue);
   if(acquired){
    // Check every mutex before consuming anything in a wait-all transaction.
    unsigned begin=all?0:first,end=all?count:(first==~0u?0:first+1);bool overflow=false;
    for(unsigned i=begin;i<end;++i)if((objects[i]>>16)==3&&app.kernel_mutexes[(objects[i]&65535)-1].recursion==~0u)overflow=true;
    if(overflow){Error(8);break;}
    result=all?0:(first==~0u?count:first);
    for(unsigned i=begin;i<end;++i)if(app.AcquireWaitObject(objects[i],t.id)&&result<0x80)result=0x80+(all?0:i);
    break;
   }
   if(!timeout||(deadline&&app.Now()>=deadline)){result=258;break;}
   t.block=7;app.ArmTimeout(timeout);t.deadline=deadline;app.Suspend(Idle);
  }
  t.multiwait=saved;t.alertable=saved_alert;t.block=0;t.deadline=0;t.pulse_handle=0;return result;
 }
 unsigned Status(){return app.WaitQueueStatus(app.current_thread);}
 bool Allocate(unsigned count){if(!count||count>64){Error(151);return false;}return true;}
 void SetMask(unsigned value){mask=value;}void ResetQueue(){}void PrepareQueue(){}void ProcessWake(){}void Free(){}
 unsigned KernelWait(unsigned count,const unsigned*handles,unsigned timeout,unsigned all,unsigned alert){
  // USER appends a task event; native queue state supplies that object directly.
  if(!count)return Wait(0,nullptr,timeout,all,alert);
  return Win98Multiwait::Kernel(*this,count,handles,all,timeout,alert);
 }
 unsigned WaitQueue(unsigned value,unsigned timeout,unsigned){
  message=true;mask=value;return Wait(0,nullptr,timeout,0,0)==~0u?0:1;
 }
};
unsigned Application::MultiwaitApi(ApiName name,const unsigned*a){
 MultiwaitEnvironment e(*this);
 if(API_IS(name,"WaitMessage")){
  unsigned result=Win98Multiwait::MessageOnly(e);return state==Failed||state==Exited?0:result;
 }
 const bool msg=!strncmp(name,"Msg",3),ex=strstr(name,"Ex")!=nullptr;
 unsigned count=a[0],timeout=msg?(ex?a[2]:a[3]):a[3],all=msg?(ex?a[4]&1:a[2]!=0):a[2],alert=msg?(ex?a[4]&2:0):(ex?a[4]:0);
 unsigned mask=msg?(ex?a[3]:a[4]):0,flags=msg?(ex?a[4]:all):0;
 if(msg&&(flags&~7u))return ~0u;

 unsigned handles[64]{};
 if((!msg&&!count)||count>64){last_error=151;return ~0u;}
 if(count&&(!a[1]||!virtual_memory.Access(a[1],count*4,false))){last_error=87;return ~0u;}
 if(count&&!Read(a[1],handles,count*4))return ~0u;
 if(msg){e.message=true;e.flags=flags;return Win98Multiwait::Message(e,count,handles,timeout,mask,flags);}
 return Win98Multiwait::Kernel(e,count,handles,all,timeout,alert);
}
}
