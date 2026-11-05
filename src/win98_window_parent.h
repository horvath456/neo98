// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98WindowParent {
struct Node { unsigned parent=0,owner=0,task=0,style=0,extended=0,flags=0,handle=0; int x=0,y=0; };
enum Operation { Show=0,Valid,Map,Unlink,Refresh,Front,Link,Attach,Event,Place };
struct Environment {
 unsigned desktop=0,special=0,accessibility=0;
 virtual Node Read(unsigned) const=0;
 virtual void Write(unsigned,const Node&)=0;
 virtual unsigned Service(unsigned,unsigned,unsigned=0,unsigned=0,unsigned=0)=0;
};
// USER. Native handles replace original linear window pointers.
// Calls remain explicit providers; in particular Show and Position may reenter.
inline unsigned SetParent(Environment&e,unsigned h,unsigned parent) {
 unsigned flags=5;
 if(!parent)parent=e.desktop;
 if(parent==0xfffd)parent=e.special;
 const unsigned old=e.Read(h).parent, result=old?e.Read(old).handle:0;
 const unsigned visible=e.Service(Show,h,0);
 if(!e.Service(Valid,h))return 0;
 auto w=e.Read(h);
 if(w.parent!=old || (w.flags&0x10) || (e.Read(parent).flags&0x10))return 0;
 const unsigned point=e.Service(Map,old,(unsigned)w.x,(unsigned)w.y);
 const int x=(short)point,y=(short)(point>>16);
 e.Service(Unlink,h,old);
 w=e.Read(h);w.parent=parent;e.Write(h,w);
 if((w.style&0x40000000)&&w.owner&&(e.Read(w.owner).extended&8)) {
  w=e.Read(h);w.extended|=8;e.Write(h,w);
 }
 if(parent==e.desktop && !(e.Read(h).style&0x04000000)) {
  w=e.Read(h);w.style|=0x04000000;e.Write(h,w);e.Service(Refresh,h,1);
 }
 const unsigned after=parent==e.desktop && !(e.Read(h).extended&8)?e.Service(Front,h):0;
 e.Service(Link,h,after,parent);
 w=e.Read(h);
 if((w.style&0xc0000000)==0x40000000) {
  if(w.parent!=e.desktop && e.Read(old).task!=w.task)e.Service(Attach,w.task,e.Read(old).task,0);
  if(parent!=e.desktop && e.Read(parent).task!=e.Read(h).task)e.Service(Attach,e.Read(h).task,e.Read(parent).task,1);
 }
 if(parent==e.special || old==e.special)flags=0x15;
 if(e.accessibility)e.Service(Event,h,0x800f);
 e.Service(Place,h,(unsigned)x,(unsigned)y,flags);
 if(visible)e.Service(Show,h,(flags&0x10)?8:1);
 return result;
}
struct Position { unsigned hwnd,after; int x,y,width,height; unsigned flags; };
static_assert(sizeof(Position)==28,"Win32 WINDOWPOS wire layout");
inline Position Raise(unsigned hwnd) {return {hwnd,0,0,0,0,0,3};} // USER
// USER selects before WM_WINDOWPOSCHANGING.
inline unsigned ActivationCandidate(const Position&p) {return p.flags&0x10?0:p.hwnd;}
// USER and 0944..0971. High internal flags are retained by USER's record.
inline bool Changing(const Position&p){return !(p.flags&0x400);}
inline bool Changed(const Position&p){return (p.flags&0x18e7)!=0x1807;}
inline unsigned ActivationKind(unsigned style,unsigned target,unsigned active,bool enabled) {
 if(!target||!enabled)return 0; // USER
 if((style&0xc0000000)==0x40000000)return 1; // WM_CHILDACTIVATE
 return target==active?0:2; // USER, activation core at USER
}
} // namespace Win98WindowParent
