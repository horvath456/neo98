// SPDX-License-Identifier: GPL-2.0-or-later
// Native Win98 COMCTL32 tab item/selection cores. E owns memory, DPA,
// strings, drawing and callbacks. Offsets retain the original object layout.
#pragma once
namespace Win98Tab {
using U=unsigned;
template<class E> U Item(E&e,U t,U index){return e.Array(332,e.Read32(t+0x28),index,0);}
template<class E> U Count(E&e,U t){return e.Read32(e.Read32(t+0x28));}
// +. Invalid-item output priority is observable.
template<class E> U Get(E&e,U t,U index,U out){
 U mask=e.Read32(out),item=Item(e,t,index);
 if(!item){if(mask&8)e.Write32(out+24,0);else if(mask&1)e.Write32(out+12,0);else if(mask&2)e.Write32(out+20,0);return 0;}
 if(mask&1){U text=e.Read32(item+0x2c);if(text)e.CopyText(e.Read32(out+12),text,e.Read32(out+16));else e.Write32(out+12,0);}
 if(mask&16){U sm=e.Read32(out+8),state=e.Read32(item+0x30)&sm;if((sm&1)&&(e.Read32(t+0x3c)==index||(e.Read32(t+0x40)==index&&(e.Read32(t+0x2c)&4))))state|=1;e.Write32(out+4,state);}
 if(mask&8)e.Copy(out+24,item+0x38,e.Read32(t+0x30));
 if(mask&2)e.Write32(out+20,e.Read32(item+0x10));
 if((mask&4)&&!(mask&1)&&e.Read32(item+0x34))e.Write32(out+16,1);
 return 1;
}
// insertion does not copy TCIF_STATE.
template<class E> U Make(E&e,U t,U source){
 U item=e.Allocate(0x38+e.Read32(t+0x30));if(!item)return 0;U mask=e.Read32(source);
 e.Write32(item+0x10,mask&2?e.Read32(source+20):~0u);e.Write32(item+0x14,0x7fffffff);e.Write32(item+0x18,0x7fffffff);
 if(mask&8)e.Copy(item+0x38,source+24,e.Read32(t+0x30));
 if(mask&1){if(!e.SetString(item+0x2c,e.Read32(source+12))){e.Free(item);return 0;}e.Write32(item+0x34,(mask&4)<<5);}
 return item;
}
template<class E> void FreeItem(E&e,U item){if(item){U text=e.Read32(item+0x2c);if(text)e.Free(text);e.Free(item);}}
// upper single-row tabs. Native scroll/draw provider receives the
// committed old/new indices; no notification is sent for a no-op or SetCurSel.
template<class E> U Select(E&e,U t,U index,bool notify){
 U old=e.Read32(t+0x3c);if(old==index)return old;
 if(int(index)>=0){U item=Item(e,t,index);if(!item||(e.Read32(item+0x30)&4))return ~0u;if(notify&&e.Notify(t,0xfffffdd8))return e.Alive(t)?e.Read32(t+0x3c):~0u;if(!e.Alive(t))return ~0u;}
 else index=~0u;
 e.Write32(t+0x3c,index);e.Selection(t,old,index);
 if(notify)e.Notify(t,0xfffffdd9);
 return old;
}
// +. Empty mask succeeds even for invalid indices.
template<class E> U Set(E&e,U t,U index,U source){
 U mask=e.Read32(source);if(!mask)return 1;U item=Item(e,t,index);if(!item)return 0;bool dirty=false,layout=false;
 if(mask&1){if(!e.SetString(item+0x2c,e.Read32(source+12)))return 0;e.Write32(item+0x34,(mask&4)<<5);dirty=layout=true;}
 if(mask&2){layout=e.Read32(item+0x10)==~0u||e.Read32(source+20)==~0u||layout;e.Write32(item+0x10,e.Read32(source+20));dirty=true;}
 if(mask&8)e.Copy(item+0x38,source+24,e.Read32(t+0x30));
 if(mask&16){U sm=e.Read32(source+8),value=e.Read32(source+4),old=e.Read32(item+0x30),state=(old&~sm)|(value&sm);e.Write32(item+0x30,state);dirty=dirty||old!=state;layout=layout||((old^state)&4);
  if((sm&1)&&!(value&1)){if(e.Read32(t+0x40)==index){e.Write32(t+0x40,~0u);dirty=true;}if(e.Read32(t+0x3c)==index){Select(e,t,~0u,true);dirty=true;if(!e.Alive(t))return 1;}}
 }
 if(dirty)e.Dirty(t,layout&&!(e.Read32(t+8)&0x400));return 1;
}
// DPA chooses and clamps the insertion index.
template<class E> U Insert(E&e,U t,U index,U source){
 U item=Make(e,t,source);if(!item)return ~0u;index=e.Array(334,e.Read32(t+0x28),index,item);if(index==~0u){FreeItem(e,item);return index;}
 U selected=e.Read32(t+0x3c);if(int(selected)<0)e.Write32(t+0x3c,index);else if(selected>=index)e.Write32(t+0x3c,selected+1);
 if(e.Read32(t+0x58)>index)e.Write32(t+0x58,e.Read32(t+0x58)+1);e.Dirty(t,true);return index;
}
// deleting selection clears it, without choosing a neighbor.
template<class E> U Delete(E&e,U t,U index){
 U item=e.Array(336,e.Read32(t+0x28),index,0);if(!item)return 0;FreeItem(e,item);
 U selected=e.Read32(t+0x3c);if(selected==index)e.Write32(t+0x3c,~0u);else if(int(selected)>int(index))e.Write32(t+0x3c,selected-1);
 if(e.Read32(t+0x58)>index)e.Write32(t+0x58,e.Read32(t+0x58)-1);e.Write32(t+0x74,~0u);e.Dirty(t,true);return 1;
}
// reverse destruction, DPA_DeleteAllPtrs, selection/scroll reset.
template<class E> U DeleteAll(E&e,U t){for(U n=Count(e,t);n;--n)FreeItem(e,Item(e,t,n-1));e.Array(337,e.Read32(t+0x28),0,0);e.Write32(t+0x3c,~0u);e.Write32(t+0x58,0);e.Dirty(t,true);return 1;}
template<class E> U Extra(E&e,U t,U bytes){if(Count(e,t)||int(bytes)<0)return 0;e.Write32(t+0x30,bytes);return 1;}
struct Metrics {int height,average,border_x,border_y;};
// Upper single-row tabs without images or buttons.
// TextWidth supplies the original prefix-aware selected-font measurement.
template<class E> void Layout(E&e,U t,const Metrics&m,int width){
 int px=e.Read32(t+0x60),py=e.Read32(t+0x64),height=e.Read32(t+0x6c),minimum=e.Read32(t+0x48);
 if(!(e.Read32(t+0x9c)&1)){minimum=2*(3*m.average+px);e.Write32(t+0x48,minimum);}
 if(!height){int spacing=2*py;if(spacing<2*m.border_y)spacing=2*m.border_y;height=m.height+m.border_y+spacing-m.border_y-1;}
 e.Write32(t+0x4c,height);e.Write32(t+0x54,m.height+2*m.border_y);e.Write32(t+0x50,width);e.Write32(t+0x98,m.height);
 int text_y=(height-m.height-3*m.border_y+1)/2;e.Write32(t+0x78,text_y);e.Write32(t+0x7c,(height-3*m.border_y)/2);
 int x=m.border_x;U n=Count(e,t);for(U i=0;i<n;++i){U item=Item(e,t,i);int text=e.TextWidth(t,item),w=e.Read32(t+8)&0x400?int(e.Read32(t+0x68)):text+2*px;if(w<minimum&&!(e.Read32(t+8)&0x400))w=minimum;
  e.Write32(item+0x1c,text);if(e.Read32(item+0x30)&4)w=0;
  e.Write32(item,x);e.Write32(item+4,m.border_y);x+=w;e.Write32(item+8,x);e.Write32(item+12,m.border_y+height);e.Write32(item+0x28,0);
  int text_x=(e.Read32(t+8)&0x400)?(w-text)/2:px;if(text_x<m.border_x)text_x=m.border_x;
  e.Write32(item+0x20,text_x);e.Write32(item+0x14,text_x);e.Write32(item+0x18,py-m.border_y/2+text_y);
 }
 e.Write32(t+0x44,x);e.Write32(t+0x70,n?0:~0u);
}
// upper standard tabs, after layout.
template<class E> void Adjust(E&e,U t,bool larger,U rect,int bx,int by){int rows=int(e.Read32(t+0x70))+1;int height=int(e.Read32(t+0x4c))*rows;int sign=larger?-1:1;e.Write32(rect,e.Read32(rect)+sign*2*bx);e.Write32(rect+4,e.Read32(rect+4)+sign*(height+2*by));e.Write32(rect+8,e.Read32(rect+8)-sign*2*bx);e.Write32(rect+12,e.Read32(rect+12)-sign*2*by);}
}
