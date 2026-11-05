// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_toolbar.h"
namespace Win98ToolbarInput {
using U=unsigned;
inline int X(U p){return short(p);}inline int Y(U p){return short(p>>16);}
template<class E> U Button(E&e,U t,U i){return e.Read32(t+0x30)+i*28;}
template<class E> void Zero(E&e,U p,U n){for(U i=0;i<n;i+=4)e.Write32(p+i,0);}
// clear the first previously checked member of this group.
template<class E> void Group(E&e,U t,U command){
 int selected=Win98Toolbar::CommandIndex(e,t,command),last=int(e.Read32(t+0x84))-1;
 if(selected<0||!(e.Read8(Button(e,t,selected)+8)&1))return;
 int first=selected,end=selected;
 while(first>0&&(e.Read8(Button(e,t,first)+9)&4))--first;
 if(!(e.Read8(Button(e,t,first)+9)&4))++first;
 while(end<last&&(e.Read8(Button(e,t,end)+9)&4))++end;
 if(!(e.Read8(Button(e,t,end)+9)&4))--end;
 for(int i=first;i<=end;++i)if(i!=selected&&(e.Read8(Button(e,t,i)+8)&1)){
  U b=Button(e,t,i);e.Write8(b+8,e.Read8(b+8)&~1u);e.InvalidateButton(t,b,1);return;
 }
}
// Hot change can be vetoed by parent.
template<class E> void Hot(E&e,U t,U index,U reason){
 if(!(e.Read32(t+8)&0x800))return;
 int old=int(e.Read32(t+0xcc)),next=int(index),count=int(e.Read32(t+0x84));
 if((old==next&&!(reason&0x40))||(old<0&&next<0)||next>=count)return;
 if(next<0&&(e.Read32(t+0xec)&0x80)&&(reason&1))return;
 typename E::Scratch note(e,24);Zero(e,note.address,24);
 if(old>=0&&old<count)e.Write32(note.address+12,e.Read32(Button(e,t,old)+4));
 if(next>=0)e.Write32(note.address+16,e.Read32(Button(e,t,next)+4));
 e.Write32(note.address+20,reason|(old<0?0x10:next<0?0x20:0));
 U hwnd=e.Read32(t);if(e.Notify(t,0xfffffd37,note.address)||!e.Alive(hwnd))return;
 old=int(e.Read32(t+0xcc));if(old!=-1)e.InvalidateButton(t,Button(e,t,old),1);
 if(next<0||!(e.Read8(Button(e,t,next)+8)&4))index=~0u;
 e.Write32(t+0xcc,index);
 if(e.Focus()==hwnd)e.Accessibility(0x8005,hwnd,~3u,index+1);
 if(index!=~0u)e.InvalidateButton(t,Button(e,t,index),1);
 if(next>=0&&next<int(e.Read32(t+0x84))&&(e.Read8(Button(e,t,next)+9)&0x88)&&(reason&0x100))e.Dropdown(t,next,0);
}
// releasing USER capture precedes NM_RELEASEDCAPTURE.
template<class E> U Release(E&e,U t){
 typename E::Scratch note(e,12);Zero(e,note.address,12);U hwnd=e.Read32(t);
 e.ReleaseCapture();e.Notify(t,~15u,note.address);return e.Alive(hwnd);
}
// Dropdown preparation is an explicit provider boundary.
template<class E> void Press(E&e,U t,U point,U index){
 if(int(index)<0||index>=e.Read32(t+0x84))return;
 U b=Button(e,t,index),hwnd=e.Read32(t);
 if(e.Read8(b+9)&0x88){e.Dropdown(t,index,1);return;}
 e.Write32(t+0x34,b);e.SetCapture(hwnd);
 U state=e.Read8(b+8);if(state&4){e.Write8(b+8,state|2);e.InvalidateButton(t,b,1);e.Update(hwnd);e.Accessibility(0x800a,hwnd,~3u,index+1);}
 e.Write32(t+0xec,e.Read32(t+0xec)&~0x200u);
 if(U pressed=e.Read32(t+0x34))e.ButtonNotify(t,e.Read32(pressed+4),0xfffffd43);
 e.Cursor(hwnd,t+0x38);
}
// Toolbar adjustment remains a provider.
template<class E> void Down(E&e,U t,U hwnd,U message,U keys,U point){
 e.Write32(t+0xec,e.Read32(t+0xec)&~0x4000u);e.Relay(t,message,keys,point);
 U index=e.Hit(t,X(point),Y(point));
 if(e.Read32(t+8)&0x20){if(e.AdjustRequested(t,keys)){e.Adjust(t,index);return;}}
 Press(e,t,point,index);
 typename E::Scratch note(e,32);Zero(e,note.address,32);
 if(int(index)>=0&&index<e.Read32(t+0x84)){U b=Button(e,t,index);e.Write32(note.address+12,e.Read32(b+4));e.Write32(note.address+16,e.Read32(b+12));}
 else e.Write32(note.address+12,~0u);
 e.Write32(note.address+20,point&65535);e.Write32(note.address+24,point>>16);e.Notify(t,~19u,note.address);
}
template<class E> void Move(E&e,U t,U hwnd,U message,U keys,U point){
 if(!(e.Read32(t+0xec)&0x40))return;U suppress=0,pressed=e.Read32(t+0x34);
 if(pressed){
  if(e.Capture()!=hwnd){
   e.ButtonNotify(t,e.Read32(pressed+4),0xfffffd42);if(!e.Alive(hwnd))return;
   pressed=e.Read32(t+0x34);if(pressed&&(e.Read8(pressed+8)&2))e.Unpress(t,e.Read32(pressed+4));
   e.Write32(t+0xec,e.Read32(t+0xec)&~0x100u);e.Write32(t+0x34,0);
  }else{
   U index=e.Hit(t,X(point),Y(point));bool inside=int(index)>=0&&Button(e,t,index)==e.Read32(t+0x34);
   if(!inside&&!(e.Read32(t+0xec)&0x200)){
    e.Write32(t+0xec,e.Read32(t+0xec)|0x200);suppress=e.ButtonNotify(t,e.Read32(e.Read32(t+0x34)+4),0xfffffd36);if(!e.Alive(hwnd))return;
   }
   pressed=e.Read32(t+0x34);
   if(pressed&&(e.Read8(pressed+8)&4)&&inside==!(e.Read8(pressed+8)&2)&&!(e.Read32(t+0xec)&0x100)){
    e.Write8(pressed+8,e.Read8(pressed+8)^2);e.InvalidateButton(t,pressed,1);
    U index2=(e.Read32(t+0x34)-e.Read32(t+0x30))/28;e.Accessibility(0x800a,hwnd,~3u,index2+1);
   }
  }
 }
 if(suppress)return;e.Relay(t,message,keys,point);
 if(e.Read32(t+8)&0x800){Hot(e,t,e.Hit(t,X(point),Y(point)),1);
  U flags=e.Read32(t+0xec);if(!(flags&0xa0)){e.Write32(t+0xec,flags|0x20);e.TrackLeave(hwnd,false);}
 }
}
// End-drag precedes WM_COMMAND.
template<class E> void Up(E&e,U t,U hwnd,U message,U keys,U point){
 e.Relay(t,message,keys,point);U index=point==~0u?~0u:e.Hit(t,X(point),Y(point));
 if((e.Read32(t+0xec)&0x4000)&&int(index)>=0)e.Write32(t+0x34,Button(e,t,index));
 if(U pressed=e.Read32(t+0x34)){
  U command=e.Read32(pressed+4);if(!Release(e,t))return;
  e.ButtonNotify(t,command,0xfffffd42);if(!e.Alive(hwnd))return;
  pressed=e.Read32(t+0x34);
  if(!pressed||!(e.Read8(pressed+8)&4)||int(index)<0||pressed!=Button(e,t,index)){e.Write32(t+0x34,0);return;}
  e.Write8(pressed+8,e.Read8(pressed+8)&~2u);U style=e.Read8(pressed+9);
  if(style&2){if(style&4){if(e.Read8(pressed+8)&1){e.Write32(t+0x34,0);return;}e.Write8(pressed+8,e.Read8(pressed+8)|1);Group(e,t,command);}else e.Write8(pressed+8,e.Read8(pressed+8)^1);}
  e.InvalidateButton(t,e.Read32(t+0x34),1);e.Write32(t+0x34,0);e.Accessibility(0x800a,hwnd,~3u,index+1);
  e.Command(e.Read32(t+4),command&65535,hwnd);if(!e.Alive(hwnd))return;
 }
 typename E::Scratch note(e,32);Zero(e,note.address,32);
 if(int(index)>=0&&index<e.Read32(t+0x84)){U b=Button(e,t,index);e.Write32(note.address+12,e.Read32(b+4));e.Write32(note.address+16,e.Read32(b+12));}
 else e.Write32(note.address+12,~0u);
 e.Write32(note.address+20,point&65535);e.Write32(note.address+24,point>>16);e.Notify(t,~1u,note.address);
}

template<class E> void Leave(E&e,U t){e.TrackLeave(e.Read32(t),true);e.Write32(t+0xec,e.Read32(t+0xec)&~0x20u);Hot(e,t,~0u,1);}
template<class E> void Cancel(E&e,U t){if(e.Read32(t+0x34)){Move(e,t,e.Read32(t),0x200,0,~0u);e.Write32(t+0x34,0);}}
// Public getter.
template<class E> void Extended(E&e,U t,U flags,U mask){
 U old=e.Read32(t+0x24),next=mask?(flags&mask)|(old&~mask):flags;bool layout=false;
 if(next&2)next=(next&~16u)|4;
 U count=e.Read32(t+0x84);
 if((next^old)&2){for(U i=0;i<count;++i){U b=Button(e,t,i);e.Write8(b+8,e.Read8(b+8)&~32u);}layout=true;}
 if((next^old)&8){for(U i=0;i<count;++i){U b=Button(e,t,i);e.Write8(b+10,0);e.Write8(b+11,0);}layout=true;e.Invalidate(e.Read32(t),0,1);}
 if((next^old)&16)e.Invalidate(e.Read32(t),0,1);
 e.Write32(t+0x24,next);if(next&4)e.ChangeStyle(t,0x80);
 if(layout){e.Layout(t);e.Reflow(t);}
}

}
