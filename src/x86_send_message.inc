// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_send_cancel.h"
#include "win98_send_message.h"
namespace X86 {
struct Application::SendState {
 // ticket is unique across processes and records. A waiter holding only block
 // and slot uses it after yielding to detect slot reuse.
 static unsigned long long tickets;
 static constexpr unsigned RecordCount=64;
 enum Operation { WindowMessage, ForegroundActivation };
 struct Record:Win98SendMessage::Record{unsigned phase=0,sender_index=0,receiver_index=0,receiver_id=0,hwnd=0,message=0,wp=0,lp=0,copy=0;Operation operation=WindowMessage;unsigned long long order=0,deadline=0,ticket=0;bool timed=false,block_sender=false,sender_done=false,wait_observed=false,sender_alive=true;Application*sender_app=nullptr,*receiver_app=nullptr;};
 Record records[RecordCount],*active[ThreadCapacity]={},*waiting[ThreadCapacity]={};unsigned long long order=0;
 // copy: the block in the receiver's memory that holds the copied lParam data
 // of this send (WM_COPYDATA, the strings), once the sender has gone on
 // while the window procedure still runs; the delivery frees it at its end.
 // Cross-process send records live in the receiver's block. Before that block
 // disappears, transfer an already computed reply to a per-thread slot in the
 // sender's block.
 unsigned answer[ThreadCapacity]={};bool answered[ThreadCapacity]={};
 struct Slot{Application*owner=nullptr;SendState*state=nullptr;};
 static Slot*Slots(){static Slot slots[64];return slots;}
 static Slot*SlotFor(Application&a,bool create){Slot*empty=nullptr;for(unsigned i=0;i<64;++i){auto&s=Slots()[i];if(s.owner==&a)return &s;if(!s.owner&&!empty)empty=&s;}if(create&&empty){auto*state=new SendState;if(!state)return nullptr;empty->owner=&a;empty->state=state;return empty;}return nullptr;}

 static SendState*Find(Application&a,bool create){auto*s=SlotFor(a,create);return s?s->state:nullptr;}
 // Before freeing a block, remove other processes' references to its records.
 // SentMessageReady reads waiting from the scheduler.
 static void Forget(SendState*dead){
  if(!dead)return;
  for(unsigned i=0;i<64;++i){auto*live=Slots()[i].state;if(!live||live==dead)continue;
   for(auto&p:live->active)if(p>=dead->records&&p<dead->records+RecordCount)p=nullptr;
   for(unsigned index=0;index<ThreadCapacity;++index){auto*&p=live->waiting[index];
    if(!p||p<dead->records||p>=dead->records+RecordCount)continue;
    live->answer[index]=(p->flags&1)?p->result:Win98SendCancel::VanishedResult(p->message);
    live->answered[index]=true;p=nullptr;}}
 }
 // Visit every pending session record with the process owning its block. The
 // session-wide list is distributed across process blocks.
 template<class F> static void EachRecord(F f){
  for(unsigned i=0;i<64;++i){auto&slot=Slots()[i];if(!slot.state||!slot.owner)continue;
   for(auto&r:slot.state->records)if(r.phase)f(*slot.owner,r);}
 }
 // For same-process sends, the block owner is the sender because the record
 // does not store a separate process.
 static Application*SenderOf(Application&owner,const Record&r){return r.sender_app?r.sender_app:&owner;}
 // Clear the sender reference before abandoning a record. The receiver then
 // owns cleanup and cannot access the departed sender.
 // Win98SendMessage::Finish releases records marked sender-gone or abandoned.
 static void DropSender(Record&r){r.sender_alive=false;r.sender_app=nullptr;r.sender_index=0;r.sender=0;r.sender_done=true;}

 struct Reference {unsigned process=0,slot=0;unsigned long long ticket=0;};
 static Reference Identify(Record*record){
  if(!record)return {};
  for(unsigned i=0;i<64;++i){auto&entry=Slots()[i];if(!entry.owner||!entry.state)continue;
   for(unsigned n=0;n<RecordCount;++n)if(&entry.state->records[n]==record)
    return {entry.owner->process_id,n,record->ticket};}
  return {};
 }
 static Application*Owner(const Reference&ref){return ref.process?static_cast<Application*>(User::ProcessContextOf(ref.process)):nullptr;}
 static Record*Resolve(const Reference&ref){
  auto*owner=Owner(ref);if(!owner||ref.slot>=RecordCount||!ref.ticket)return nullptr;
  auto*state=Find(*owner,false);auto*record=state?&state->records[ref.slot]:nullptr;
  return record&&record->phase&&record->ticket==ref.ticket?record:nullptr;
 }
 Record*Next(unsigned index){Record*first=nullptr;for(auto&r:records)if(r.phase==1&&r.receiver_index==index&&(!first||r.order<first->order))first=&r;return first;}
 struct Completion{Record&r;void Wake(){}void Dispose(){r.phase=3;if(r.sender_done)r=Record{};}void CallbackReply(unsigned){}};
};
// These allocations belong to the thread, not to a suspended host stack.
// Identities resolve a reused or freed receiver block without dangling pointers.
struct Application::SendWait {
 SendWait*previous=nullptr;
 SendState::Reference record,previous_wait;
 unsigned previous_answer=0,message=0;
 bool previous_answered=false,recorded=false;
};
struct Application::SendDelivery {
 SendDelivery*previous=nullptr;
 SendState::Reference record,previous_active;
 unsigned extra=0;
 bool previous_waiting=false,recorded=false;
};
bool Application::SentWaitGate(unsigned gate) const {
 if(gate<GateBase||gate>=GateBase+import_count*16||(gate-GateBase)%16)return false;
 const auto&item=imports[(gate-GateBase)/16];
 if(Win16Process())return !strcmp(item.dll,"USER")&&(item.id==108||item.id==109||item.id==112);
 if(strcasecmp(item.dll,"USER32.DLL"))return false;
 static const char*const names[]={"GetMessageA","GetMessageW","PeekMessageA","PeekMessageW","WaitMessage","MsgWaitForMultipleObjects","MsgWaitForMultipleObjectsEx"};
 for(const char*name:names)
  if(!strcmp(item.name,name))return true;
 return false;
}
unsigned Application::FinishSendTimeout(unsigned output,unsigned result,bool completed){
 if(!completed||state==Failed||state==Exited)return 0;
 if(output&&!Store(output,&result,4))return 0;
 return 1;
}
unsigned Application::FinishSendCopy(unsigned process,unsigned block,unsigned result,unsigned output,unsigned capacity){
 if(auto*receiver=static_cast<Application*>(User::ProcessContextOf(process))){
  if(output&&capacity&&threads[current_thread].sent_completed){
   // WM_GETTEXT returns characters excluding the terminator. Only those
   // bytes belong to the reply; preserve the rest of the sender's buffer.
   const unsigned bytes=result<capacity?result+1:capacity;
   unsigned char buffer[512];
   for(unsigned at=0;at<bytes;at+=sizeof buffer){
    const unsigned part=bytes-at<sizeof buffer?bytes-at:unsigned(sizeof buffer);
    if(!receiver->Read(block+at,buffer,part)||!Store(output+at,buffer,part))break;
   }
  }
  // After ReplyMessage or a timeout the sender goes on while the window
  // procedure still runs with the copy. USER keeps it until that procedure
  // returns: TAPISRV answers a request with ReplyMessage and reads dwData
  // of its COPYDATASTRUCT afterwards. The delivery frees the block then.
  if(auto*s=SendState::Find(*receiver,false))
   for(auto&r:s->records)if(r.phase==2&&r.lp==block&&!r.copy){r.copy=block;return result;}
  receiver->Free(block);
 }
 return result;
}
bool Application::HasSentMessages(unsigned index){auto*s=SendState::Find(*this,false);return s&&s->Next(index);}
unsigned Application::SentQueueStatus(unsigned index,bool observe){
 auto*s=SendState::Find(*this,false);if(!s)return 0;
 unsigned status=0;
 for(auto&r:s->records)if(r.phase==1&&r.receiver_index==index){
  status|=0x00400000;if(!r.wait_observed)status|=0x40;if(observe)r.wait_observed=true;
 }
 return status;
}
// The thread of another process this thread waits for in SendMessage, while
// the receiver has not taken the message yet (HandSendOver).
Application*Application::SendReceiver(unsigned index,unsigned&receiver){
 auto*s=SendState::Find(*this,false);if(!s)return nullptr;
 auto*r=s->waiting[index];
 if(!r||r->phase!=1||(r->flags&1)||!r->receiver_app||r->receiver_app==this||r->receiver_index>=ThreadCapacity)return nullptr;
 receiver=r->receiver_index;return r->receiver_app;
}
bool Application::SentMessageReady(unsigned index){
 auto*s=SendState::Find(*this,false);if(!s)return false;
 auto*r=s->waiting[index];
 if(!r||(r->flags&1)||(r->timed&&Now()>=r->deadline))return true;
 if(!r->block_sender&&s->Next(index))return true;
 // Wake senders when their target window, receiver process or receiver thread
 // disappears. No reply can arrive afterwards; leaving the sender blocked can
 // stall the desktop.
 if(!User::IsWindow(r->hwnd))return true;
 auto*receiver_app=r->receiver_app?r->receiver_app:this;
 if(receiver_app->state==Exited||receiver_app->state==Failed)return true;
 auto&receiver=receiver_app->threads[r->receiver_index];return receiver.done||receiver.id!=r->receiver_id;
}
unsigned Application::FinishSentDelivery(SendDelivery*delivery,unsigned result){
 auto&t=threads[current_thread];
 if(t.sent_delivery!=delivery){Fail("Received send continuation chain is inconsistent");return 0;}
 if(auto*state=SendState::Find(*this,false))state->active[current_thread]=SendState::Resolve(delivery->previous_active);
 t.message_metadata.extra=delivery->extra;
 auto*record=SendState::Resolve(delivery->record);
 // ExitThread/ExitProcess may have detached or removed this exact record.
 // A newly reused slot never receives the old callback's result.
 // The copy of a sender that has gone on (FinishSendCopy).
 unsigned copy=0;
 if(record&&record->phase==2){copy=record->copy;record->copy=0;record->phase=3;SendState::Completion completion{*record};Win98SendMessage::Finish(completion,*record,result);}
 if(copy)Free(copy);
 t.host_waiting=delivery->previous_waiting;
 t.sent_delivery=delivery->previous;
 if(delivery->recorded)PopHostFrame();
 delete delivery;
 return result;
}
unsigned Application::ResumeSentDelivery(unsigned process,unsigned slot,unsigned low,unsigned high){
 auto&t=threads[current_thread];
 const unsigned long long ticket=low|(static_cast<unsigned long long>(high)<<32);
 auto*delivery=t.sent_delivery;
 while(delivery&&(delivery->record.process!=process||delivery->record.slot!=slot||delivery->record.ticket!=ticket))delivery=delivery->previous;
 if(!delivery||!delivery->recorded){Fail("Saved received send has no matching continuation");return 0;}
 ++t.host_resume_at;
 return FinishSentDelivery(delivery,ResumeInner());
}
void Application::ProcessSentMessages(){
 auto*s=SendState::Find(*this,false);if(!s)return;
 if(auto*r=s->waiting[current_thread];r&&r->block_sender)return;
 auto&t=threads[current_thread];
 while(state!=Failed&&state!=Exited&&!t.done){
  auto*r=s->Next(current_thread);if(!r)break;
  auto*delivery=new SendDelivery;if(!delivery){last_error=8;return;}
  delivery->previous=t.sent_delivery;
  delivery->record=SendState::Identify(r);
  delivery->previous_active=SendState::Identify(s->active[current_thread]);
  delivery->extra=t.message_metadata.extra;
  delivery->previous_waiting=t.host_waiting;
  // A sender or modal loop has a recorded turn of its own. A first delivery
  // inside a plain USER wait instead returns to that wait, not through it.
  const unsigned below=depth?t.call_marks[depth].frames:0;
  const unsigned top=t.host_frame_count?t.host_frames[t.host_frame_count-1].kind:HostNone;
  const bool outer_record=t.host_frame_count>below&&
      ((t.host_waiting&&(top==HostModal||top==HostMenu||top==HostFrameTrack))||top==HostSentWait);
  const bool again=t.host_frame_count==below&&SentWaitGate();
  delivery->recorded=(outer_record||again)&&PushHostFrame(HostSentDelivery,delivery->record.process,delivery->record.slot,
      unsigned(delivery->record.ticket),unsigned(delivery->record.ticket>>32));
  t.sent_delivery=delivery;t.host_waiting=false;
  r->phase=2;r->flags|=0x10;s->active[current_thread]=r;
  if(r->sender_alive)t.message_metadata.extra=(r->sender_app?r->sender_app:this)->threads[r->sender_index].message_metadata.extra;
  GiveTail(delivery->recorded?(again?TailAgain:TailRecord):TailNone);
  unsigned result=0;
  if(User::IsWindow(r->hwnd)&&User::WindowThread(r->hwnd)==ThreadId())
   result=r->operation==SendState::ForegroundActivation?SwitchForeground(r->hwnd,r->wp):Send(r->hwnd,r->message,r->wp,r->lp);
  GiveTail(TailNone);
  FinishSentDelivery(delivery,result);
  s=SendState::Find(*this,false);if(!s)break;
 }
}
unsigned Application::SendCrossThread(unsigned hwnd,unsigned message,unsigned wp,unsigned lp,unsigned timeout,unsigned flags,bool*completed){
 return SendCrossProcessRaw(this,hwnd,message,wp,lp,timeout,flags,completed);
}
unsigned Application::QueueForegroundActivation(unsigned hwnd,unsigned flags){
 const unsigned process=User::WindowProcess(hwnd),id=User::WindowThread(hwnd);
 auto*other=process&&process!=process_id?static_cast<Application*>(User::ProcessContextOf(process)):this;
 if(!other||!User::IsWindow(hwnd))return 0;
 unsigned target=ThreadCapacity;
 for(unsigned i=0;i<ThreadCapacity;++i)
  if(other->threads[i].id==id&&other->threads[i].fiber&&!other->threads[i].done){target=i;break;}
 if(target==ThreadCapacity)return 0;
 const auto&t=other->threads[target];
 const bool asleep=t.block==2||t.block==5||(t.block==7&&!t.multiwait.message);
 if(!asleep&&ThreadId()!=close_program_thread_id)
  return SendCrossProcessRaw(other,hwnd,0,flags,0,0,0,nullptr,true);
 auto*s=SendState::Find(*other,true);if(!s){last_error=8;return 0;}
 SendState::Record*r=nullptr;for(auto&v:s->records)if(!v.phase){r=&v;break;}
 if(!r){last_error=8;return 0;}
 *r=SendState::Record{};
 r->phase=1;r->flags=4;r->receiver_index=target;r->receiver_id=id;r->hwnd=hwnd;r->wp=flags;
 r->operation=SendState::ForegroundActivation;
 r->order=++s->order;r->ticket=++SendState::tickets;r->receiver_app=other;
 r->sender_alive=false;r->sender_done=true;
 return 1;
}
// SendNotifyMessage. For a window of the calling thread it is a
// plain SendMessage; for any other thread the message goes into the
// receiver's list of sent messages and the call returns at once, TRUE. The
// record is born the way USER leaves one whose sender has gone: no
// sender queue, so it belongs to the receiver, InSendMessageEx answers
// ISMEX_NOTIFY (USER, sender zero and neither callback bit), and
// USER disposes of it after the window procedure (flag 4). A sender
// that waited here hung Explorer's shell start: a SHELL32 thread notified the
// desktop thread while that thread waited for an event the notifier sets.
// WM_COPYDATA keeps the synchronous path, whose copy lives only for the send.
unsigned Application::SendNotify(unsigned hwnd,unsigned message,unsigned wp,unsigned lp){
 if(!User::IsWindow(hwnd)){last_error=1400;return 0;}
 const unsigned process=User::WindowProcess(hwnd),id=User::WindowThread(hwnd);
 auto*other=process&&process!=process_id?static_cast<Application*>(User::ProcessContextOf(process)):this;
 if(!other){last_error=1400;return 0;}
 // The answer is success, not what the window procedure returned.
 if((other==this&&(!id||!Win98SendMessage::CrossTask(id,ThreadId())))||message==0x4a){
  Send(hwnd,message,wp,lp);return state==Failed?0:1;}
 unsigned target=ThreadCapacity;for(unsigned i=0;i<ThreadCapacity;++i)if(other->threads[i].id==id&&other->threads[i].fiber&&!other->threads[i].done){target=i;break;}
 if(target==ThreadCapacity){last_error=1400;return 0;}
 auto*s=SendState::Find(*other,true);if(!s){last_error=8;return 0;}
 SendState::Record*r=nullptr;for(auto&v:s->records)if(!v.phase){r=&v;break;}if(!r){last_error=8;return 0;}
 *r=SendState::Record{};
 r->phase=1;r->sender=0;r->flags=4;r->receiver_index=target;r->receiver_id=id;r->hwnd=hwnd;r->message=message;r->wp=wp;r->lp=lp;
 r->order=++s->order;r->ticket=++SendState::tickets;r->receiver_app=other==this?nullptr:other;
 r->sender_alive=false;r->sender_done=true;
 return 1;
}
// A window of another process: the record lives in the receiver's state so
// its thread finds it in ProcessSentMessages; the sender waits as it would
// for another thread of its own. Pointers in lParam are not translated,
// except the strings of WM_SETTEXT, WM_WININICHANGE and WM_DEVMODECHANGE,
// and the output string of WM_GETTEXT.
// WM_COPYDATA crosses the address spaces: USER copies the COPYDATASTRUCT and
// its bytes into the receiver's memory for the duration of the send.
// Native composition may run on a different process's resting fiber. The
// arguments already belong to this receiver (including DRAWITEMSTRUCT).
unsigned Application::SendFromNative(unsigned hwnd,unsigned message,unsigned wp,unsigned lp){
 auto*sender=static_cast<Application*>(User::ProcessContextOf(User::CurrentProcess()));
 return sender&&sender!=this?sender->SendCrossProcessRaw(this,hwnd,message,wp,lp):Send(hwnd,message,wp,lp);
}
unsigned Application::SendCrossProcess(unsigned hwnd,unsigned message,unsigned wp,unsigned lp,unsigned timeout,unsigned flags,bool*completed){
 const unsigned tail=TakeTail();
 auto*other=static_cast<Application*>(User::ProcessContextOf(User::WindowProcess(hwnd)));
 if(!other||other==this){last_error=50;return 0;}
 const unsigned receiver_process=other->process_id;
 auto copied=[&](unsigned block,unsigned output=0,unsigned capacity=0)->unsigned{
  const bool recorded=(tail==TailResult||tail==TailRecord)&&PushHostFrame(HostSendCopy,receiver_process,block,output,capacity);
  GiveTail(recorded?TailRecord:TailNone);
  const unsigned result=SendCrossProcessRaw(other,hwnd,message,wp,block,timeout,flags,completed);
  if(recorded)PopHostFrame();
  return FinishSendCopy(receiver_process,block,result,output,capacity);
 };
 // USER's synchronous system messages carry their pointer data across
 // processes. A foreign window must write WM_GETTEXT into its own memory;
 // the reply is copied back before the sender resumes.
 if(message==0x0d&&wp&&lp){
  if(!virtual_memory.Access(lp,wp,true)){last_error=87;return 0;}
  const unsigned block=other->Allocate(wp);
  if(!block){last_error=8;return 0;}
  other->Write32(block,0);
  return copied(block,lp,wp);
 }
 // WM_SETTEXT, WM_WININICHANGE and WM_DEVMODECHANGE carry a string. Under
 // Win98 the message goes through USER.EXE, where the string is valid for
 // the receiver; here it is copied into the receiver's memory for the
 // duration of the send, like the block of WM_COPYDATA below (the
 // section name of SystemParametersInfo's broadcast reached CALC as a pointer
 // into Explorer's heap).
 if((message==0x0c||message==0x1a||message==0x1b)&&lp){
  // A value that is no string of the sender goes as it is (Read would end
  // the sender); the probe is the one of IsBadStringPtr.
  const unsigned probe[2]={lp,1u<<20};
  unsigned n=0;bool readable=!Api("IsBadStringPtrA",probe);
  if(readable)for(char c=1;c&&n<(1u<<20);++n)if(!Read(lp+n,&c,1)){readable=false;break;}
  if(readable){
   auto*text=static_cast<char*>(malloc(n));
   const unsigned block=text&&Read(lp,text,n)?other->Allocate(n):0;
   if(!block){free(text);last_error=8;return 0;}
   text[n-1]=0;other->Store(block,text,n);
   free(text);
   return copied(block);
  }
 }
 if(message!=0x4a||!lp){GiveTail(tail);return SendCrossProcessRaw(other,hwnd,message,wp,lp,timeout,flags,completed);}
 unsigned copy[3]={};if(!Read(lp,copy,12)){last_error=87;return 0;}
 const unsigned bytes=copy[1]>16u*1024*1024?0:copy[1];
 const unsigned block=other->Allocate(12+bytes);if(!block){last_error=8;return 0;}
 const unsigned mirrored[3]={copy[0],bytes,bytes?block+12:0};
 other->Store(block,mirrored,12);
 if(bytes){auto*data=static_cast<unsigned char*>(malloc(bytes));if(data&&Read(copy[2],data,bytes))other->Store(block+12,data,bytes);free(data);}
 return copied(block);
}
unsigned Application::SendCrossProcessRaw(Application*other,unsigned hwnd,unsigned message,unsigned wp,unsigned lp,unsigned timeout,unsigned flags,bool*completed,bool activation){
 const unsigned tail=TakeTail();
 if(completed)*completed=false;
 if(!other){last_error=1400;return 0;}
 unsigned target=ThreadCapacity,id=User::WindowThread(hwnd);
 for(unsigned i=0;i<ThreadCapacity;++i)if(other->threads[i].id==id&&other->threads[i].fiber&&!other->threads[i].done){target=i;break;}
 if(target==ThreadCapacity){last_error=1400;return 0;}
 auto*s=SendState::Find(*other,true);auto*mine=SendState::Find(*this,true);
 if(!s||!mine){last_error=8;return 0;}
 SendState::Record*r=nullptr;for(auto&v:s->records)if(!v.phase){r=&v;break;}
 if(!r){last_error=8;return 0;}
 auto*wait=new SendWait;if(!wait){last_error=8;return 0;}
 *r=SendState::Record{};
 r->phase=1;r->sender=ThreadId();r->sender_index=current_thread;r->receiver_index=target;r->receiver_id=id;
 r->hwnd=hwnd;r->message=message;r->wp=wp;r->lp=lp;r->order=++s->order;r->sender_app=this;r->receiver_app=other;
 if(activation)r->operation=SendState::ForegroundActivation;
 r->ticket=++SendState::tickets;r->timed=completed!=nullptr;
 r->deadline=Now()+static_cast<unsigned long long>(timeout)*1000;r->block_sender=(flags&1)!=0;
 auto&t=threads[current_thread];
 wait->previous=t.sent_wait;wait->record=SendState::Identify(r);wait->message=message;
 wait->previous_wait=SendState::Identify(mine->waiting[current_thread]);
 wait->previous_answer=mine->answer[current_thread];wait->previous_answered=mine->answered[current_thread];
 wait->recorded=(tail==TailResult||tail==TailRecord)&&PushHostFrame(HostSentWait,wait->record.process,wait->record.slot,
      unsigned(wait->record.ticket),unsigned(wait->record.ticket>>32));
 t.sent_wait=wait;mine->waiting[current_thread]=r;mine->answered[current_thread]=false;
 return ContinueSentWait(wait,false,completed);
}
unsigned Application::ResumeSentWait(unsigned process,unsigned slot,unsigned low,unsigned high){
 auto&t=threads[current_thread];
 const unsigned long long ticket=low|(static_cast<unsigned long long>(high)<<32);
 auto*wait=t.sent_wait;
 while(wait&&(wait->record.process!=process||wait->record.slot!=slot||wait->record.ticket!=ticket))wait=wait->previous;
 if(!wait||!wait->recorded){Fail("Saved sender wait has no matching continuation");return 0;}
 ++t.host_resume_at;
 return ContinueSentWait(wait,true);
}
unsigned Application::ContinueSentWait(SendWait*wait,bool resumed,bool*completed){
 auto&t=threads[current_thread];
 if(resumed){
  // A blocked send may itself have been delivering a nested received send.
  if(t.host_resume_level<t.host_resume_top||t.host_resume_at<t.host_frame_count)ResumeInner();
  else t.host_resume=false;
 }
 SendState::Record*r=nullptr;
 bool lost=false;
 for(;;){
  r=SendState::Resolve(wait->record);
  if(!r||!r->sender_alive){lost=true;break;}
  if(auto*mine=SendState::Find(*this,false))mine->waiting[current_thread]=r;
  if((r->flags&1)||state==Failed||state==Exited||t.done)break;
  ProcessSentMessages();
  r=SendState::Resolve(wait->record);
  if(!r||!r->sender_alive){lost=true;break;}
  if(r->flags&1)break;
  if(r->timed&&Now()>=r->deadline){last_error=0;break;}
  auto*receiver=SendState::Owner(wait->record);
  if(r->phase==1&&!User::IsWindow(r->hwnd)){
   r->result=Win98SendCancel::VanishedResult(r->message);r->flags|=1;r->phase=3;break;}
  if(!receiver||receiver->state==Failed||receiver->state==Exited||receiver->threads[r->receiver_index].done||receiver->threads[r->receiver_index].id!=r->receiver_id){
   r->result=Win98SendCancel::VanishedResult(r->message);r->flags|=1;if(r->phase==1)r->phase=3;break;}
  t.block=8;t.deadline=0;t.send_window=r->hwnd;t.host_waiting=wait->recorded;
  Suspend(Idle);
  t.host_waiting=false;t.send_window=0;
 }
 unsigned result=0;bool answered=false;
 auto*mine=SendState::Find(*this,false);
 if(lost){
  if(mine&&mine->answered[current_thread]){result=mine->answer[current_thread];answered=true;}
  else {last_error=1400;result=Win98SendCancel::VanishedResult(wait->message);}
 }else{
  answered=(r->flags&1)!=0;result=r->result;r->sender_done=true;
  if(r->phase==2)r->flags|=8;else *r=SendState::Record{};
 }
 if(mine){mine->waiting[current_thread]=SendState::Resolve(wait->previous_wait);
  mine->answer[current_thread]=wait->previous_answer;mine->answered[current_thread]=wait->previous_answered;}
 t.sent_completed=answered;if(completed)*completed=answered;
 if(t.sent_wait!=wait){Fail("Sender wait continuation chain is inconsistent");return 0;}
 t.sent_wait=wait->previous;
 if(wait->recorded)PopHostFrame();
 delete wait;
 return result;
}
// Answer pending sends during window destruction instead of waiting for a
// receiver that can no longer retrieve them.
void Application::CancelSendsToWindow(unsigned hwnd){
 if(!hwnd)return;
 SendState::EachRecord([&](Application&owner,SendState::Record&r){
  (void)owner;
  if(r.hwnd!=hwnd)return;
  unsigned result=r.result;
  switch(Win98SendCancel::WindowDestroyed(r.flags,r.message,!r.sender_done,result)){
   case Win98SendCancel::Skip:break;
   case Win98SendCancel::Abandon:break;   // Replied and abandoned; receiver owns cleanup.
   case Win98SendCancel::Callback:break;  // No callback sender is implemented.
   case Win98SendCancel::ReplyOnly:
   case Win98SendCancel::ReplyWake:
    r.result=result;if(r.phase==1)r.phase=3;
    break;
  }
 });
}
// At process queue shutdown, cancel outgoing sends first, then incoming sends
// for all its threads.
void Application::CancelSendLifetimes(){
 SendState::EachRecord([&](Application&owner,SendState::Record&r){
  if(SendState::SenderOf(owner,r)!=this)return;
  const auto action=Win98SendCancel::SenderGone(r.flags);
  SendState::DropSender(r);
  if(action==Win98SendCancel::Free)r=SendState::Record{};
 });
 auto*s=SendState::Find(*this,false);if(!s)return;
 for(auto&r:s->records){
  if(!r.phase)continue;
  const auto decision=Win98SendCancel::ReceiverGone(r.flags,r.message,!r.sender_done);
  if(decision.store)r.result=decision.result;
  if(decision.wake){if(r.phase==1)r.phase=3;continue;}
  if(decision.free_record)r=SendState::Record{};
 }
}
// Cancel only sends to or from the ending thread. Other threads' records
// remain active.
void Application::CancelSendsOfThread(unsigned index){
 if(index>=ThreadCapacity)return;
 const unsigned id=threads[index].id;
 SendState::EachRecord([&](Application&owner,SendState::Record&r){
  if(SendState::SenderOf(owner,r)!=this||r.sender_index!=index)return;
  const auto action=Win98SendCancel::SenderGone(r.flags);
  SendState::DropSender(r);
  if(action==Win98SendCancel::Free)r=SendState::Record{};
 });
 auto*s=SendState::Find(*this,false);if(!s)return;
 for(auto&r:s->records){
  if(!r.phase||r.receiver_index!=index||(id&&r.receiver_id!=id))continue;
  const auto decision=Win98SendCancel::ReceiverGone(r.flags,r.message,!r.sender_done);
  if(decision.store)r.result=decision.result;
  if(decision.wake){if(r.phase==1)r.phase=3;continue;}
  if(decision.free_record)r=SendState::Record{};
 }
}
unsigned Application::SentMessageApi(ApiName name,const unsigned*a){auto*s=SendState::Find(*this,false);auto*r=s?s->active[current_thread]:nullptr;
 if(API_IS(name,"InSendMessage"))return Win98SendMessage::InSend(r);
 if(API_IS(name,"InSendMessageEx"))return Win98SendMessage::InSendEx(r);
 if(!r)return 0;SendState::Completion e{*r};return Win98SendMessage::Reply(e,r,a[0]);
}
// Diagnostics for the crash report: what the blocked sender is waiting for.
void Application::SentWaitText(unsigned index,char*out,unsigned capacity){
 auto*s=SendState::Find(*this,false);auto*r=s&&index<ThreadCapacity?s->waiting[index]:nullptr;
 if(!r){snprintf(out,capacity,"Reply to SendMessage; no pending record");return;}
 snprintf(out,capacity,"Reply to SendMessage %04x to window %08x, recipient thread %u (slot %u), phase %u%s",
          r->message,r->hwnd,r->receiver_id,r->receiver_index,r->phase,
          r->receiver_app&&r->receiver_app!=this?", another process":"");
}
void Application::FreeSentMessages(){
 // Resolve all records before the block disappears. Remove outgoing records
 // from foreign blocks so their receivers cannot later read a freed sender's
 // thread table.
 CancelSendLifetimes();
 for(auto&thread:threads){
  while(thread.sent_wait){auto*old=thread.sent_wait;thread.sent_wait=old->previous;delete old;}
  while(thread.sent_delivery){auto*old=thread.sent_delivery;thread.sent_delivery=old->previous;delete old;}
 }
 auto*s=SendState::SlotFor(*this,false);if(s){SendState::Forget(s->state);delete s->state;*s=SendState::Slot{};}
}
unsigned long long Application::SendState::tickets=0;

}
