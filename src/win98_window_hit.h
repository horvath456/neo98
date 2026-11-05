// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98WindowHit {
using U=unsigned;
inline int S(U x){return short(x);}
struct Point{int x,y;};struct Rect{int left,top,right,bottom;};
struct Window{U next=0,child=0,parent=0,style=0,extended=0,flags=0,task=0,region=0,kind=0,combo=0;Rect outer={},client={};};
inline bool Contains(Rect r,Point p){return p.x>=r.left&&p.y>=r.top&&p.x<r.right&&p.y<r.bottom;}
// Original STATIC USER and BUTTON USER WM_NCHITTEST branches.
inline U ControlHit(U kind,U style,U fallback){return kind==3?(style&0x100?1:U(-1)):kind==2&&(style&15)==7?U(-1):fallback;}
inline Point Narrow(Point p){return {S(p.x),S(p.y)};}
// Providers: Read, Valid, Desktop, RegionContains, ModalAllowed/ModalWindow,
// CurrentTask, HitMessage, ComboChild and sizing-border metrics.
// USER; kind1 is the original ComboBox class identity.
template<class E>U Child(E&e,U h,Point p,U flags){
 auto w=e.Read(h);p=Narrow(p);flags&=65535;
 if(!(w.flags&0x40)&&w.kind==1&&p.x==1&&p.y==1){U child=e.ComboChild(h);if(child)return child;}
 if(h!=e.Desktop())p={S(p.x+w.client.left),S(p.y+w.client.top)};
 if(!Contains(w.client,p)||(w.region&&!e.RegionContains(w.region,p)))return 0;
 for(U child=w.child;child;){w=e.Read(child);
  if(!((flags&1)&&!(w.style&0x10000000))&&!((flags&2)&&(w.style&0x08000000))&&!((flags&4)&&(w.extended&0x20))&&Contains(w.outer,p)&&(!w.region||e.RegionContains(w.region,p)))return child;
  child=w.next;
 }
 return h;
}
// USER. BUTTON/BS_GROUPBOX is remembered as a fallback.
template<class E>U RealChild(E&e,U h,Point p){
 if(!e.Valid(h))return 0;auto w=e.Read(h);p=Narrow(p);
 if(h!=e.Desktop())p={S(p.x+w.client.left),S(p.y+w.client.top)};
 if(!Contains(w.client,p)||(w.region&&!e.RegionContains(w.region,p)))return 0;
 U group=0;for(U child=w.child;child;){w=e.Read(child);
  if((w.style&0x10000000)&&Contains(w.outer,p)&&(!w.region||e.RegionContains(w.region,p))){if(w.kind==2&&(w.style&15)==7)group=child;else return child;}
  child=w.next;
 }
 return group?group:h;
}
// Complete original sizing-parent helper USER.
template<class E>U SizeParent(E&e,U h){auto initial=e.Read(h);U p=h;for(;;){auto w=e.Read(p);
 if(w.style&0x40000){if(w.style&0x01000000)return 0;return S(initial.outer.right+e.SizeX())>=w.client.right&&S(initial.outer.bottom+e.SizeY())>=w.client.bottom?p:0;}
 if(!(w.style&0x40000000)||(w.flags&0x080000)||!w.parent)return 0;p=w.parent;
 }}
// USER; callback dispatcher is an explicit provider.
template<class E>U Search(E&e,U h,Point p,U disabled,int*hit=nullptr){
 if(!h)return 0;auto w=e.Read(h);
 if(!(w.style&0x10000000)||!Contains(w.outer,p)||(w.region&&!e.RegionContains(w.region,p)))return 0;
 int result;
 if((w.style&0x08000000)&&disabled){if((w.style&0xc0000000)==0x40000000)return 0;result=-2;}
 else{
  if(!e.ModalAllowed(h)){h=e.ModalWindow();w=e.Read(h);}
  if(!(w.style&0x20000000)){
   if(Contains(w.client,p))for(U child=w.child;child;){U saved_next=e.Read(child).next;U found=Search(e,child,p,disabled,hit);if(found)return found;child=e.Valid(child)?e.Read(child).next:saved_next;}
   w=e.Read(h);if(w.task!=e.CurrentTask())result=1;else result=S(e.HitMessage(h,p));
  }else result=S(e.HitMessage(h,p));
  if(result==-1)return 0;
 }
 if(hit)*hit=result;
 if(result==17&&!(e.Read(h).style&0x40000)){U parent=SizeParent(e,h);if(parent)return parent;}
 return h;
}
// USER validates the final pointer after callback reentry.
template<class E>U FromPoint(E&e,Point p){U h=Search(e,e.Desktop(),Narrow(p),1);return e.Valid(h)?h:0;}
}
