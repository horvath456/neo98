// SPDX-License-Identifier: GPL-2.0-or-later
// Native providers for the shared USER capture queue. Included inside User.
namespace {
void*capture_context=nullptr;CaptureNotify capture_notify=nullptr;
struct CaptureEnvironment final:Win98Capture::Environment {
 CaptureEnvironment():Environment(capture_globals){}
 // A queue per thread; the queue of a call is the calling thread's
 // unless the operation names another (acting).
 unsigned acting=~0u;
 Win98Capture::Queue ReadQueue(unsigned q)const override {
  const auto*slot=CaptureSlotFor(q,false);auto value=slot?*slot:Win98Capture::Queue{};
  value.menu_owner=CaptureMenuState(value.menu_flags,value.menu_present);return value;
 }
 void WriteQueue(unsigned q,const Win98Capture::Queue&value)override {if(auto*slot=CaptureSlotFor(q,true)){*slot=value;++capture_serial;}}
 Win98Capture::Window ReadWindow(unsigned h)const override {return IsWindow(h)?Win98Capture::Window{WindowThread(h),h,0x40}:Win98Capture::Window{};}
 unsigned CurrentQueue()const override{return acting!=~0u?acting:CurrentThread();}
 unsigned Service(unsigned op,unsigned h,unsigned a,unsigned)override {
  using namespace Win98Capture;
  if(op==Notify){
   automatic_capture=false;
   // A subclass may keep BM_SETSTATE latched while its popup owns capture.
   // Deliver WM_CAPTURECHANGED before changing BUTTON state behind its back.
   if(auto*w=Find(h)){if(w->button&&!capture_notify)w->pressed=false;dirty=true;}
   if(capture_notify&&IsWindow(h))capture_notify(CallbackContext(capture_context,h),h,a);
  }else if(op==Win98Capture::EndMenu)User::EndMenu();
  // CAPTUREEND for the old window, CAPTURESTART for the
  // new one, attributed to the window's queue.
  else if(op==Win98Capture::Accessibility)RaiseWinEvent(a,h,0,0,true);
  // Native hover tracking and move/size drag clipping are not enabled in this
  // provider. Their original branches remain oracle-tested.
  return 0;
 }
};
}
void SetCaptureNotify(void*p,CaptureNotify callback){capture_context=p;capture_notify=callback;}
namespace {bool win_events=false;void*win_event_context=nullptr;WinEventCallback win_event=nullptr;}
void SetWinEventCallback(void*p,WinEventCallback callback){win_event_context=p;win_event=callback;}
void SetWinEventListening(bool on){win_events=on;capture_globals.accessibility=on?1:0;}
bool WinEventListening(){return win_events;}
void RaiseWinEvent(unsigned event,HWND h,unsigned object,unsigned child,bool window_queue){
 if(win_events&&win_event)win_event(CurrentContext(win_event_context),event,h,object,child,window_queue);
}
// The queue whose capture a menu loop locked (flag 800h), the caller's first.
static unsigned MenuCaptureQueue()
{
 if(ThreadCaptureFlags(CurrentThread())&0x800)return CurrentThread();
 for(const auto&slot:capture_slots)if(slot.used&&(slot.queue.flags&0x800))return slot.thread;
 return CurrentThread();
}
// 0 GetCapture, 1 SetCapture, 2 ReleaseCapture of the calling thread. The
// native callers act for a window and name its queue: 1 sets the capture in
// the queue of the window (a native control takes it during the routing of
// the press, on no guest thread), 3 changes it there, 4 and 5 are the menu
// loop's lock, 6 the destruction of the window, 7 WM_CANCELMODE, 8 the
// window the menu loop holds, 9 the release of the window's queue.
unsigned CaptureOperation(unsigned op,HWND h,unsigned mode)
{
 CaptureEnvironment e;
 if(op==0)return Win98Capture::Get(e);
 if(op==1){if(h&&(!IsWindow(h)||h==Desktop))return 0;automatic_capture=false;if(h)e.acting=WindowThread(h);return Win98Capture::Set(e,h);}
 if(op==2){automatic_capture=false;return Win98Capture::Release(e);}
 if(op==9){automatic_capture=false;e.acting=WindowThread(h);return Win98Capture::Release(e);}
 if(op==4){automatic_capture=false;e.acting=WindowThread(h);Win98Capture::MenuSet(e,h);}
 if(op==5){automatic_capture=false;e.acting=MenuCaptureQueue();return Win98Capture::MenuRelease(e);}
 if(op==8){e.acting=MenuCaptureQueue();return Win98Capture::Get(e);}
 if(op==6){
  for(const auto&slot:capture_slots)if(slot.used&&slot.queue.capture==h){
   const HWND held=QueueCapture(slot.thread);if(held==h)automatic_capture=false;
   Win98Capture::Change(e,slot.thread,0,0x8004);
  }
 }
 if(op==7){e.acting=WindowThread(h);Win98Capture::Cancel(e,h);}
 if(op==3)Win98Capture::Change(e,WindowThread(h),h,mode);
 return 0;
}
void BeginControlCapture(HWND h)
{
 const unsigned serial=capture_serial;CaptureOperation(1,h,0);
 automatic_capture=QueueCapture(WindowThread(h))==h&&capture_serial==serial+1;
}
