// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_toolbar_layout.h"
namespace Win98ToolbarPaint {
using U=unsigned;
// inline string, shared string-table index, or no text.
template<class E> U Text(E&e,U t,U b){U p=e.Read32(b+16);if(p==~0u)return 0;if(p&0xffff0000)return p;return int(p)<int(e.Read32(t+0x5c))?e.Read32(e.Read32(t+0x58)+p*4):0;}
// This is split-dropdown padding, not image presence.
template<class E> U SplitArrow(E&e,U t,U b){U style=e.Read8(b+9);return (e.Read8(t+0x24)&1)&&(style&8)&&!(style&128);}

template<class E> U TextHeight(E&e,U t,U bitmap_height){
 U height=e.Read32(t+0x88);
 if(e.Read32(t+8)&0x1000){
  if(e.Read8(t+0x24)&8){height=0;for(U i=0;int(i)<int(e.Read32(t+0x84));++i){
   U b=e.Read32(t+0x30)+i*28,v=e.Read32(t+0x6c);
   if((e.Read8(b+9)&64)&&int(e.Read32(t+0x88))>int(v))v=e.Read32(t+0x88);
   if(int(v)>=int(height))height=v;
  }}
  return int(bitmap_height)>int(height)?bitmap_height:height;
 }
 return height?bitmap_height+height+1:bitmap_height;
}
// No-change returns before invalidation.
template<class E> U BitmapSize(E&e,U t,U width,U height){
 if(!width)width=1;if(!height)height=1;
 if(width==~0u)width=e.Read32(t+0x68);if(height==~0u)height=e.Read32(t+0x6c);
 if(e.Read32(t+0x68)==width&&e.Read32(t+0x6c)==height)return 1;
 U required=height;if(e.Read32(t+0x5c)||(e.Read8(t+0xec)&8))required=TextHeight(e,t,height);
 if(!e.SetSize(t,width,required,1))return 0;
 e.Write32(t+0x68,width);e.Write32(t+0x6c,height);
 e.Invalidate(e.Read32(t),0,1);e.Write32(t+0xec,e.Read32(t+0xec)&~1u);return 1;
}
// three image-list kinds per group.
template<class E> U SetImages(E&e,U t,U kind,U index,U list){
 if(int(e.Read32(t+0x1c))<5||int(index)<0||int(index)>=20)index=0;
 if(int(index)>=int(e.Read32(t+0xc8))){
  U next=Win98Toolbar::ResizeAllocation(e,e.Read32(t+0xc4),(index+1)*12);
  if(next){U count=e.Read32(t+0xc8);e.Write32(t+0xc4,next);
   for(U i=count*12;i<(index+1)*12;++i)e.Write8(next+i,0);e.Write32(t+0xc8,index+1);}
 }
 if(int(index)>=int(e.Read32(t+0xc8)))return 0;
 U p=e.Read32(t+0xc4)+(index*3+kind)*4,old=e.Read32(p);e.Write32(p,list);return old;
}
template<class E> U Images(E&e,U t,U kind,U index){return int(index)<0||int(index)>=int(e.Read32(t+0xc8))?0:e.Read32(e.Read32(t+0xc4)+(index*3+kind)*4);}
// Setting padding does not invalidate by itself.
template<class E> U Padding(E&e,U t,U packed){U old=(e.Read32(t+0x94)&65535)|(e.Read32(t+0x98)<<16);int x=short(packed),y=short(packed>>16);if(x!=-1)e.Write32(t+0x94,U(x));if(y!=-1)e.Write32(t+0x98,U(y));return old;}
// Hidden wrap markers still count.
template<class E> U Rows(E&e,U t){U rows=1;for(U i=0;i<e.Read32(t+0x84);++i){U b=e.Read32(t+0x30)+i*28;if(e.Read8(b+8)&32)rows+=(e.Read8(b+9)&1)?2:1;}return rows;}
// /. Misses preserve original insertion codes.
template<class E> U Hit(E&e,U t,int x,int y){
 U count=e.Read32(t+0x84);if(!count)return ~0u;U following=0,last_y=0;typename E::Scratch r(e,16);
 for(U n=0;n<16;++n)e.Write8(r.address+n,0); // Native defines original uninitialized all-hidden case.
 for(U i=0;int(i)<int(count);++i){
  if(!Win98ToolbarLayout::Rect(e,t,i,r.address))continue;
  if((e.Read8(t+0x24)&16)&&e.Clipped(t,r.address))continue;
  if(y<int(e.Read32(r.address+4))||y>=int(e.Read32(r.address+12))){last_y=i;continue;}
  if(x>=int(e.Read32(r.address))&&x<int(e.Read32(r.address+8)))return (e.Read8(e.Read32(t+0x30)+i*28+9)&1)?~0u-i:i;
  following=i+1;
 }
 if(following)return ~0u-following;
 return y>int(e.Read32(r.address+12))?~0u-count:last_y+1;
}
} // namespace Win98ToolbarPaint
