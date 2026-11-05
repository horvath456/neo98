// SPDX-License-Identifier: GPL-2.0-or-later
#include "x86_app.h"
#include <string.h>
namespace X86 {
// The system timer USER sets on a popup window for the submenu under a
// resting pointer, and the delay of SPI_GETMENUSHOWDELAY on Win98 (400 ms,
// "MenuShowDelay" of Control Panel\Desktop).
static constexpr unsigned MenuShowTimer=0xfffe,MenuShowDelay=400;
unsigned Application::RunMenuLoop(){
 auto&t=threads[current_thread];const unsigned thread=current_thread;
 bool kept=false,idle_sent=false;unsigned menu_frame=~0u;
 const auto finish_turn=[&](){if(User::MenuBeepRequest()){const unsigned beep=0;Api("MessageBeep",&beep);}};
 if(t.host_resume&&t.host_resume_at<t.host_frame_count&&t.host_frames[t.host_resume_at].kind==HostMenu){
  menu_frame=t.host_resume_at;idle_sent=t.host_frames[menu_frame].a!=0;
  kept=true;if(++t.host_resume_at>=t.host_frame_count)t.host_resume=false;
  if(t.host_resume_level<t.host_resume_top||t.host_resume){ResumeInner();finish_turn();}
 }else if(t.host_frame_count&&(t.host_frames[t.host_frame_count-1].kind==HostPopupMenu||t.host_frames[t.host_frame_count-1].kind==HostBarMenu)){
  kept=PushHostFrame(HostMenu);if(kept)menu_frame=t.host_frame_count-1;
 }
 // The same loop serves the bar while only a bar item is selected.
 while((User::PopupActive()||User::BarModeActive())&&state!=Failed&&state!=Exited){
  if(const unsigned hover=User::PopupHoverRequest()){const unsigned t[]={hover,MenuShowTimer,MenuShowDelay,0};TimerApi("SetTimer",t);}
  if(User::DispatchPopupInput())continue;
  // An input packet can end the menu: a key or a click that chooses an item
  // posts its command. The packets are dispatched while the loop
  // looks for its next message; if the menu is over then, that message, the
  // command among them, stays for the program's own loop, which gets it
  // after WM_EXITMENULOOP and with the bar item no longer selected.
  User::Message message{};
  const bool waiting=ModalMessage(message,false);
  // USER's loop leaves at once when an item is chosen and never
  // looks at the command it posted. The look above did, and a look marks
  // what it sees as seen: a program whose own loop goes from DispatchMessage
  // straight to WaitMessage (Visual Basic 3) then slept on its WM_COMMAND
  // until the pointer moved, with the menu's pixels still up. What
  // stays in the queue for the program counts as new again.
  if((!User::PopupActive()&&!User::BarModeActive())||state==Failed||state==Exited){User::UnobserveQueue();break;}
  if(!waiting||!ModalMessage(message)){
   // WM_DRAWITEM may close the menu. Do not enter an indefinite message
   // wait after that callback has already completed the tracking.
   User::ServeControlPaints();
   if((!User::PopupActive()&&!User::BarModeActive())||state==Failed||state==Exited)break;
   unsigned owner=0,popup=0;
   if(!idle_sent&&User::MenuIdleTarget(owner,popup)){
    idle_sent=true;if(kept)t.host_frames[menu_frame].a=1;
    GiveTail(kept?TailRecord:TailNone);Send(owner,0x121,2,popup);
    // The owner may close the menu or post WM_CANCELMODE from WM_ENTERIDLE.
    continue;
   }
   threads[thread].host_waiting=kept;Suspend(Idle);threads[thread].host_waiting=false;continue;
  }
  idle_sent=false;if(kept)t.host_frames[menu_frame].a=0;
  MessageHook(3,0,1,message);if(state==Failed||state==Exited)break;
  if(message.message==0x113&&message.wparam==MenuShowTimer&&User::PopupHoverOpen(message.hwnd)){const unsigned t[]={message.hwnd,MenuShowTimer};TimerApi("KillTimer",t);continue;}
  if(message.message==0x12){User::PostMessage(0,0x12,message.wparam,0);User::EndMenu();break;}
  if(!User::PopupInputMessage(message)){User::TranslateMessage(&message);GiveTail(kept?TailRecord:TailNone);Send(message.hwnd,message.message,message.wparam,message.lparam);}
  finish_turn();
 }
 if(User::PopupActive())User::EndMenu();if(kept)PopHostFrame();return User::PopupCommand();
}
unsigned Application::FinishBarMenu(unsigned result,bool keyboard){
 User::ResumeBarTracking(result);
 if(keyboard&&User::MenuBeepRequest()){const unsigned beep=0;Api("MessageBeep",&beep);}
 PopHostFrame();return 0;
}
// WM_MEASUREITEM with the original MEASUREITEMSTRUCT field order. The struct
// lives in guest memory and the owner answers on its own thread.
void Application::MeasureMenuItem(unsigned owner,unsigned id,unsigned data,int*cx,int*cy){
 unsigned values[6]={1,0,id,unsigned(*cx),unsigned(*cy),data};
 unsigned p=Allocate(sizeof values);if(!p)return;
 if(Store(p,values,sizeof values)){
  SendFromNative(owner,0x2c,0,p);
  if(Read(p,values,sizeof values)){*cx=int(values[3]);*cy=int(values[4]);}
 }
 Free(p);
}
// WM_DRAWITEM with the original DRAWITEMSTRUCT field order.
void Application::DrawMenuItem(unsigned owner,const unsigned*fields){
 unsigned values[12];
 for(unsigned n=0;n<7;++n)values[n]=fields[n];
 for(unsigned n=0;n<4;++n)values[7+n]=fields[7+n];
 values[11]=fields[11];
 unsigned p=Allocate(sizeof values);if(!p)return;
 if(Store(p,values,sizeof values))SendFromNative(owner,0x2b,0,p);
 Free(p);
}
unsigned Application::MenuTrackingApi(ApiName name,const unsigned*a){
 switch (name.id)
 {
 API_CASE("GetMenuBarInfo"){unsigned fields[8];if(!Read(a[3],fields,4)||fields[0]!=32||!Read(a[3],fields,sizeof fields))return 0;bool result=User::MenuBarInfo(a[0],int(a[1]),int(a[2]),fields);return Store(a[3],fields,sizeof fields)&&result;}
 API_CASE("EndMenu")return User::EndMenu();
 API_CASE("GetMenuItemRect"){Gdi::RECT rect{};bool okay=User::MenuItemRect(a[0],a[1],a[2],&rect);return Store(a[3],&rect,sizeof rect)&&okay;}
 API_CASE("MenuItemFromPoint")return User::MenuItemFromPoint(a[0],a[1],int(a[2]),int(a[3]));
 default:
     break;
 }
 unsigned owner=API_IS(name,"TrackPopupMenu")?a[5]:a[4];Gdi::RECT exclude{};const Gdi::RECT*rectangle=nullptr;
 if(API_IS(name,"TrackPopupMenuEx")&&a[5]){unsigned p[5];if(!Read(a[5],p,sizeof p)||p[0]!=20)return 0;exclude={int(p[1]),int(p[2]),int(p[3]),int(p[4])};rectangle=&exclude;}
 const bool gate=TakeFresh();
 const bool kept=gate&&PushHostFrame(HostPopupMenu,a[1]);
 const unsigned result=User::TrackPopup(a[0],a[1],int(a[2]),int(a[3]),owner,rectangle);
 if(kept)PopHostFrame();return result;
}
}
