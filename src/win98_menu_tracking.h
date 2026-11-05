// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_menu_placement.h"
namespace Win98MenuTracking {
using Win98Menu::U;using Win98Menu::W;using Win98Menu::S;
using Win98MenuGeometry::Rect;using Win98MenuPlacement::Point;
struct Popup {U owner=0,window=0,menu=0,parent=0;W flags=0,animation=0,signature=0x5550,auxiliary=0;S selected=-1;};
struct Session {Popup*current=nullptr;W flags=0,thread=0,command=0,extra=0;S input_mode=0;U owner=0;};
struct Environment {
 virtual ~Environment()=default;
 virtual W CurrentThread()=0;virtual W WindowThread(U)=0;
 virtual Session*CurrentSession()=0;
 virtual bool IsWindow(U)=0;virtual bool IsMenu(U)=0;
 virtual W MenuFlags(U)=0;virtual bool ButtonDown(W)=0;
 virtual Popup*CreatePopup(U menu,U owner,Point at,bool owned_window)=0;
 virtual Session*BeginSession(Popup&,W thread)=0;
 virtual void FinishSession(Session*)=0;
 virtual U Send(U,W,W,U)=0;virtual void Post(U,W,W,U)=0;
 virtual bool PostCommands(U)=0;
 virtual void Select(Popup&,S)=0;
 virtual void CloseSubmenus(Session&,Popup&)=0;
 virtual void DestroyPopup(Popup&)=0;
 virtual U CaptureOwner()=0;virtual void SetCapture(Popup&)=0;virtual void ReleaseCapture()=0;
 virtual U WindowMenu(U)=0;virtual bool MenuAnimationEnabled()=0;virtual void StartMenuAnimation(Popup&)=0;
 virtual void Accessibility(W,U)=0;
 virtual Rect WorkArea(Point)=0;
 virtual S BorderX()=0;virtual S BorderY()=0;
 virtual void AnimationSetting(W)=0;
 virtual void PositionPopup(Popup&,Point,S width,S height,bool no_activate)=0;
 virtual void UpdatePopup(Popup&)=0;
 virtual W DispatchLoop(Session&,Popup&)=0;
};
// USER. Selection state is captured before callbacks can change it.
inline void Close(Environment&e,Session&s,W message,W command,U parameter,W extra=0){
 Popup*p=s.current;if(!p)return;const U owner=p->owner;const W flags=p->flags;
 bool return_command=flags&0x200,popup=flags&8,system=flags&4,notify=!(flags&0x1000);
 if(p->signature!=0x5550||p->parent)return;
 s.flags&=W(~0xc);p->auxiliary|=4;
 if(e.CurrentThread()!=s.thread||(p->auxiliary&2))return;
 p->auxiliary|=2;e.CloseSubmenus(s,*p);e.Select(*p,-1);s.flags&=W(~1);e.ReleaseCapture();
 if(popup){e.Accessibility(7,p->window);e.DestroyPopup(*p);}
 if(!owner)return;
 e.Send(owner,0x11f,0,0xffffu);e.Accessibility(5,owner);
 if(notify)e.Send(owner,0x212,W(popup&&!system),0);
 if(message){e.AnimationSetting(0x9d);s.command=command;
  if(!return_command){if(!system&&popup&&!e.PostCommands(owner))e.Send(owner,message,command,parameter);else e.Post(owner,message,command,parameter);}
 }else s.command=0;
 (void)extra; // Native message dispatch has no Win16 send mode argument.
}
// USER. Reentrant EndMenu sets cancellation before cleanup.
inline bool End(Environment&e){auto*s=e.CurrentSession();if(!s||!s->current)return e.CurrentThread()!=0;auto*p=s->current;s->flags&=W(~5);p->auxiliary|=4;if(e.CaptureOwner()==p->owner)e.ReleaseCapture();
 if(e.IsWindow(p->owner)){if(!(p->auxiliary&2))Close(e,*s,0,0,0);}else{bool popup=p->flags&8;e.CloseSubmenus(*s,*p);if(popup)s->current=nullptr;}return true;
}
// USER, popup branch of menu input initialization. Menu-bar
// switching (flags bit0) is a separate caller path and is not inferred here.
inline bool InitializePopup(Environment&e,Session&s,Popup&p){
 if(p.auxiliary&4)return false;s.input_mode=-1;s.flags|=1;
 s.flags=W((s.flags&~0x10)|(e.ButtonDown(1)?0x10:0));s.flags=W((s.flags&~8)|((s.flags&0x10)?8:0));
 e.SetCapture(p);e.Send(p.owner,0x20,W(p.owner),2);s.flags=W((s.flags&~2)|((p.flags&4)?2:0));
 if(!(p.flags&0x1000)){U menu=(p.flags&4)?e.WindowMenu(p.owner):p.menu;e.Send(p.owner,0x116,W(menu),0);
  if(!e.IsMenu(p.menu)){s.flags&=W(~1);e.ReleaseCapture();return false;}}
 if(!(p.auxiliary&4)){W flags=e.MenuFlags(p.menu);if(flags&0x20)s.flags|=0x40;if((flags&0x40)&&e.MenuAnimationEnabled())s.flags|=0x100;if(flags&0x80)s.flags|=0x200;if(flags&0x100)s.extra|=1;}
 e.Accessibility(4,p.owner);return !(p.auxiliary&4);
}
// USER. Popup allocation, drawing, monitor and input queue mechanics
// are providers; flags, callback order, validation and return decisions are ported.
inline U Track(Environment&e,U menu,U owner,Point at,W flags,const Rect*exclude=nullptr,U parameter_size=12){
 if(exclude&&parameter_size!=12)return 0;
 W thread=e.CurrentThread();auto*previous=e.CurrentSession();
 if(previous){if(!(flags&1)||!(previous->flags&4)||!previous->current||!previous->current->owner||previous->current->owner!=owner||previous->thread!=e.WindowThread(owner))return 0;}
 if(e.WindowThread(owner)!=thread)return 0;
 bool pressed=e.ButtonDown((flags&2)?2:1);
 Popup*p=e.CreatePopup(menu,owner,at,(e.MenuFlags(menu)&0x20)!=0);if(!p)return 0;
 p->owner=owner;p->menu=menu;p->auxiliary|=8;p->flags|=8;
 p->flags=W((p->flags&~0x400)|(pressed?0x400:0));p->flags=W((p->flags&~0x80)|((flags&2)?0x80:0));p->flags=W((p->flags&~0x1000)|((flags&0x80)?0x1000:0));
 bool return_command=flags&0x100;if(return_command)p->flags|=0x200;p->flags=W((p->flags&~4)|((flags&0x200)?4:0));
 Session*s=e.BeginSession(*p,thread);bool started=s!=nullptr;
 auto failed=[&](){e.Accessibility(5,owner);e.ReleaseCapture();if(e.IsWindow(p->window))e.DestroyPopup(*p);if(started&&!(flags&0x80))e.Send(owner,0x212,W(!(flags&0x200)),0);e.FinishSession(s);return 0u;};
 if(!s)return failed();
 if(!(p->flags&0x1000))e.Send(owner,0x211,W(!(p->flags&4)),0);
 InitializePopup(e,*s,*p);if(s->flags&0x100)e.StartMenuAnimation(*p);
 if(!(p->flags&0x1000)){e.Send(owner,0x117,W(menu),(p->flags&4)?0xffff0000u:0);p->auxiliary|=0x20;}
 if(!e.IsMenu(menu))return failed();
 U size=e.IsWindow(p->window)?e.Send(p->window,0x403,1,0):0;if(!size)return failed();
 S width=S(W(size)+W(e.BorderX()*2)),height=S(W(size>>16)+W(e.BorderY()*2));Rect work=e.WorkArea(at);
 Point position=Win98MenuPlacement::Align(at,width,height,flags,p->flags,p->animation);
 position=Win98MenuPlacement::Place(work,position,width,height,exclude,flags,p->animation);if(p->animation)p->animation|=0x8000;
 e.AnimationSetting(0x9e);e.PositionPopup(*p,position,width,height,!(s->flags&0x40));e.Accessibility(6,p->window);e.UpdatePopup(*p);
 s->current=p;s->flags=W((s->flags&~8)|(pressed?8:0));W result=e.DispatchLoop(*s,*p);
 if(!(s->flags&0x40))e.FinishSession(s);return return_command?result:1;
}
}
