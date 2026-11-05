// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98Capture {
struct Queue {unsigned flags=0,capture=0,mode=0,menu_owner=0,menu_flags=0,menu_present=0;};
struct Window {unsigned queue=0,handle=0,flags=0;};
struct Globals {unsigned tracking=0,tracked=0,special=0,constrained=0,marker=0,outline=0,accessibility=0,scroll=0;};
enum Operation {ClearTracking=0,MouseRefresh,Accessibility,Notify,EraseDrag,Unclip,FinishDrag,CancelScroll,EndMenu};
struct Environment {
 Globals&globals;explicit Environment(Globals&g):globals(g){}
 virtual Queue ReadQueue(unsigned)const=0;virtual void WriteQueue(unsigned,const Queue&)=0;
 virtual Window ReadWindow(unsigned)const=0;virtual unsigned CurrentQueue()const=0;
 virtual unsigned Service(unsigned,unsigned,unsigned=0,unsigned=0)=0;
};
// USER. Publish state before accessibility and capture-change callbacks.
inline void Change(Environment&e,unsigned queue,unsigned h,unsigned mode){
 auto&g=e.globals;const unsigned old=e.ReadQueue(queue).capture,tracked=g.tracked;
 if((g.tracking&&tracked&&h==tracked&&(mode&15)!=2)||(old==tracked&&e.ReadQueue(queue).mode!=2))e.Service(ClearTracking,queue);
 if(!(mode&15)){if(g.special&&e.ReadWindow(g.special).queue==queue)g.special=0;}
 else if((mode&15)==5){g.special=h;mode=(mode&~15u)|2;}
 if(!h&&old)e.Service(MouseRefresh,0);
 auto q=e.ReadQueue(queue);q.capture=h;q.mode=mode&15;e.WriteQueue(queue,q);
 if(g.accessibility){if(old)e.Service(Accessibility,old,9);if(h)e.Service(Accessibility,h,8);}
 if(!(mode&0x8000)&&old&&(e.ReadWindow(old).flags&0x40))e.Service(Notify,old,h?e.ReadWindow(h).handle:0);
}
// USER. Public capture accepts only the caller's input queue.
inline unsigned Set(Environment&e,unsigned h){
 const unsigned queue=e.CurrentQueue();auto q=e.ReadQueue(queue);
 if((q.flags&0x800)||(h&&e.ReadWindow(h).queue!=queue))return 0;
 const unsigned result=q.capture?e.ReadWindow(q.capture).handle:0;Change(e,queue,h,2);return result;
}
// USER. Menu capture blocks public release until its helper unlocks.
inline unsigned Release(Environment&e){
 const unsigned queue=e.CurrentQueue();if(e.ReadQueue(queue).flags&0x800)return 0;
 auto&g=e.globals;
 if(g.constrained&&g.special&&e.ReadWindow(g.special).queue==queue){
  e.Service(EraseDrag,2);g.marker=0;e.Service(Unclip,0);if(!g.outline)e.Service(FinishDrag,0);g.constrained=0;
 }
 Change(e,queue,0,0);return 1;
}
// USER, corresponding direct USER32 getter.
inline unsigned Get(Environment&e){unsigned h=e.ReadQueue(e.CurrentQueue()).capture;return h?e.ReadWindow(h).handle:0;}
// USER. Internal menu capture uses the same queue as public capture.
inline void MenuSet(Environment&e,unsigned h){unsigned id=e.CurrentQueue();Change(e,id,h,4);auto q=e.ReadQueue(id);q.flags|=0x800;e.WriteQueue(id,q);}
inline unsigned MenuRelease(Environment&e){unsigned id=e.CurrentQueue();auto q=e.ReadQueue(id);q.flags&=~0x800u;e.WriteQueue(id,q);return Release(e);}
// USER. The freed owner's final transition suppresses notification.
inline void Destroy(Environment&e,unsigned h){unsigned id=e.ReadWindow(h).queue;if(e.ReadQueue(id).capture==h)Change(e,id,0,0x8004);}
// USER, dispatched for WM_CANCELMODE.
inline void Cancel(Environment&e,unsigned h){
 const unsigned id=e.CurrentQueue();
 if(e.ReadQueue(id).capture==h&&e.globals.scroll)e.Service(CancelScroll,h,1);
 auto q=e.ReadQueue(id);if(q.menu_present&&q.menu_owner==h&&!(q.menu_flags&0x40))e.Service(EndMenu,h);
 if(e.ReadQueue(id).capture==h)Release(e);
}
}
