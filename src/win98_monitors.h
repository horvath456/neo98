// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Windows 98 multiple-monitor decisions. USER32's MonitorFromPoint/Rect/Window,
// GetMonitorInfoA and EnumDisplayMonitors use the 16-bit USER implementation.
// This port provides its own handles through src/x86_monitors.cpp.
// Coordinates are signed WORDs; callers truncate them.
namespace Win98Monitors {
using U=unsigned;
struct Rect{int left=0,top=0,right=0,bottom=0;};
struct Info{U size=0;Rect monitor,work;U flags=0;char device[32]={};};
// Every comparison and subtraction below runs in a 16-bit register.
inline int W(int value){return short(value);}
inline bool Empty(const Rect&r){return W(r.right)<=W(r.left)||W(r.bottom)<=W(r.top);}
inline bool Inside(const Rect&r,int x,int y){return W(x)>=W(r.left)&&W(y)>=W(r.top)&&W(x)<W(r.right)&&W(y)<W(r.bottom);}
inline bool Meet(Rect&out,const Rect&a,const Rect&b){
 const int l=W(a.left)>W(b.left)?W(a.left):W(b.left),t=W(a.top)>W(b.top)?W(a.top):W(b.top);
 const int r=W(a.right)<W(b.right)?W(a.right):W(b.right),d=W(a.bottom)<W(b.bottom)?W(a.bottom):W(b.bottom);
 if(r<=l||d<=t){out={};return false;}
 out={l,t,r,d};return true;
}
// USER. The original gap is measured per axis as an inclusive WORD
// distance, zero when the axes overlap; the monitor with the smallest larger
// gap wins and ties keep the earlier index.
template<class E> U Nearest(E&e,const Rect&rc){
 int best=0x7fff;U chosen=0;
 for(U i=0;!(e.Count()<i);++i){
  const Rect m=e.Monitor(i);int dx,dy;
  if(!(W(rc.right)>W(m.left)))dx=W(W(m.left)-W(rc.right)+1);
  else if(!(W(m.right)>W(rc.left)))dx=W(W(rc.left)-W(m.right)+1);
  else dx=0;
  if(!(W(rc.bottom)>W(m.top)))dy=W(W(m.top)-W(rc.bottom)+1);
  else if(!(W(rc.top)<W(m.bottom)))dy=W(W(rc.top)-W(m.bottom)+1);
  else dy=0;
  const int gap=dy<dx?dx:dy;
  if(gap<best){best=gap;chosen=i;}
 }
 return e.Handle(chosen);
}
// USER. The containing monitor wins; otherwise MONITOR_DEFAULTTOPRIMARY
// takes the first record and MONITOR_DEFAULTTONEAREST searches from the
// degenerate rectangle {x,y,x,y} - not from a one-pixel rectangle.
template<class E> U FromPoint(E&e,int x,int y,U flags){
 for(U i=0;!(e.Count()<i);++i)if(Inside(e.Monitor(i),x,y))return e.Handle(i);
 if(flags&1)return e.Handle(0);
 if(flags&2){const Rect r{W(x),W(y),W(x),W(y)};return Nearest(e,r);}
 return 0;
}
// USER. A single display short-circuits both default flags, an empty
// rectangle degrades to its own upper-left point, and a rectangle that covers
// the whole virtual screen answers primary without any intersection work.
// Otherwise the largest intersection area wins, a full-size intersection
// returns at once, and the area comparison is unsigned over the signed
// WORD*WORD product, so a wrapped negative product beats every positive one.
template<class E> U FromRect(E&e,const Rect&rc,U flags){
 if(!e.Count()&&(flags&3))return e.Handle(0);
 if(Empty(rc))return FromPoint(e,rc.left,rc.top,flags);
 const Rect vs=e.VirtualScreen();
 if(!(W(rc.left)>W(vs.left))&&!(W(rc.top)>W(vs.top))&&!(W(rc.right)<W(vs.right))&&!(W(rc.bottom)<W(vs.bottom)))return e.Handle(0);
 const int width=W(W(rc.right)-W(rc.left)),height=W(W(rc.bottom)-W(rc.top));
 U chosen=0xffff,best=0;
 for(U i=0;!(e.Count()<i);++i){
  Rect t;if(!Meet(t,rc,e.Monitor(i)))continue;
  const int h=W(W(t.bottom)-W(t.top)),w=W(W(t.right)-W(t.left));
  if(w==width&&h==height)return e.Handle(i);
  const U area=U(w*h);
  if(area>best){best=area;chosen=i;}
 }
 if(chosen!=0xffff)return e.Handle(chosen);
 if(flags&1)return e.Handle(0);
 if(flags&2)return Nearest(e,rc);
 return 0;
}
// USER. An invalid window still honours the default flags. A window
// carrying the original monitor property (USER, atom at DGROUP 0x386)
// is pinned to that rectangle; otherwise a direct child of the desktop answers
// primary and every other window uses its outer rectangle at WND+0x10.
template<class E> U FromWindow(E&e,U hwnd,U flags){
 const U object=e.Validate(hwnd);
 if(!object)return flags&3?e.Handle(0):0;
 if(!e.Count()&&(flags&3))return e.Handle(0);
 if(e.HasProperties(object)){
  Rect pinned;
  if(e.MonitorProperty(object,pinned))return FromRect(e,pinned,flags);
  if(e.ParentIsDesktop(object))return e.Handle(0);
 }
 return FromRect(e,e.WindowRect(object),flags);
}
// USER. Only MONITORINFO and MONITORINFOEX sizes are accepted, both
// rectangles are sign-expanded WORD by WORD, MONITORINFOF_PRIMARY marks the
// first record and only the long form receives a device name.
template<class E> U Query(E&e,U index,Info&info){
 if(info.size!=0x28&&info.size!=0x48)return 0;
 const Rect m=e.Monitor(index),w=e.Work(index);
 info.monitor={W(m.left),W(m.top),W(m.right),W(m.bottom)};
 info.work={W(w.left),W(w.top),W(w.right),W(w.bottom)};
 info.flags=index?0:1;
 if((info.size>>16)||(info.size&0xffff)>=0x48)e.Name(index,info.device);
 return 1;
}
// USER. Without a DC the area starts as the virtual screen narrowed
// by the optional clip. A DC supplies its already clipped screen rectangle.
// Every monitor that meets the area reports its intersection. A callback returning zero
// stops the walk, and a monitor list rebuilt during the callback forces a
// zero result even when the callback agreed to continue.
template<class E,class F> U Enumerate(E&e,const Rect*clip,F callback,bool display_context=false){
 Rect area=display_context&&clip?*clip:e.VirtualScreen();
 if(clip&&!display_context){Rect meet;Meet(meet,area,*clip);area=meet;}
 U result=1;
 for(U i=0;!(e.Count()<i);++i){
  Rect t;if(!Meet(t,area,e.Monitor(i)))continue;
  const U sequence=e.Sequence();
  result=callback(e.Handle(i),t);
  if(e.Sequence()!=sequence)result=0;
  if(!result)break;
 }
 return result;
}
struct Export{const char*name;U ordinal,argc;};
inline constexpr Export Exports[]={{"EnumDisplayMonitors",194,4},{"GetMonitorInfoA",296,2},{"GetMonitorInfoW",297,2},
 {"MonitorFromPoint",438,3},{"MonitorFromRect",439,2},{"MonitorFromWindow",440,2}};
}
