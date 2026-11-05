// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Original COMCTL32 toolbar geometry. Isolated work in progress.
#include "win98_toolbar.h"
namespace Win98ToolbarLayout {
using U=unsigned;
// /. Signed multiply wraps before division by three.
template<class E> U SeparatorHeight(E&e,U t,U b){U v=e.Read32(b);return e.Read16(t+8)&0x880?v:U(int(v*2)/3);}
// Compute toolbar layout.
template<class E> bool HasArrow(E&e,U t,U b){return ((e.Read8(t+0x24)&1)&&(e.Read8(b+9)&8))||(e.Read8(b+9)&128);}
template<class E> bool TextArrow(E&e,U t,U b){return !(e.Read8(t+9)&16)&&(e.Read8(b+9)&128)&&int(e.Read32(t+0x60))>0&&e.Text(t,b);}
template<class E> U ArrowWidth(E&e,U t,U b){
 U width=0;
 if(HasArrow(e,t,b)){
  bool calculate=!TextArrow(e,t,b);
  if(!calculate)calculate=(e.Read8(b+9)&16)||((e.Read8(t+0x24)&8)&&(e.Read8(t+9)&16)&&!(e.Read8(b+9)&1));
  if(calculate){
   width=e.Read16(t+0x8c);
   if(e.Read8(b+9)&128){width-=2*e.BorderX();if(e.Read32(b)!=0xfffffffe)width-=e.BorderX();}
   if(TextArrow(e,t,b)&&int(e.TextWidth(t,b,0))>=int(e.Read32(t+0x68)+width))width=0;
  }
 }
 return int(width)>0?width:0;
}
// Cached width is a WORD.
template<class E> U Width(E&e,U t,U b,U dc){
 U style=e.Read8(b+9);
 const bool autosize=(style&16)||((e.Read8(t+0x24)&8)&&(e.Read32(t+8)&0x1000)&&!(style&1));
 if(autosize){
  if(!e.Read32(t+0x60)||((e.Read8(t+0x24)&8)&&(e.Read32(t+8)&0x1000)&&!(style&64)))e.Write16(b+10,0);
  else if(!e.Read16(b+10)){
   U measured=e.TextWidth(t,b,dc);e.Write16(b+10,e.Read16(t+0x94)+measured);
   if(measured){U padding=2*e.BorderX();if((e.Read32(t+8)&0x1000)&&e.ImagePresent(t,b))padding=e.Read16(t+0xa0);e.Write16(b+10,e.Read16(b+10)+padding);}
  }else return e.Read16(b+10);
  if(e.Read32(b)!=0xfffffffe){
   if(e.Read32(t+8)&0x1000){
    e.Write16(b+10,e.Read16(b+10)+e.Read16(t+0x68)+e.Read16(t+0x9c));
    if(!e.Read32(t+0x60)||((e.Read8(t+0x24)&8)&&(e.Read32(t+8)&0x1000)&&!(e.Read8(b+9)&64)))e.Write16(b+10,e.Read16(b+10)+2*e.BorderX());
   }else{U minimum=e.Read32(t+0x68)+e.Read32(t+0x94),width=e.Read16(b+10);if(int(width)>int(minimum))minimum=width;e.Write16(b+10,minimum);}
  }
  e.Write16(b+10,e.Read16(b+10)+ArrowWidth(e,t,b));
 }
 U width=e.Read16(b+10);if(width)return width;
 if(e.Read8(b+9)&1){
  if(e.Read8(t+8)&128){typename E::Scratch rect(e,16);e.ClientRect(e.Read32(t),rect.address);return e.Read32(rect.address+8)-e.Read32(rect.address);}
  return e.Read32(b);
 }
 if(e.Read8(t+0x24)&4)return e.Read32(t+0x70);
 return ArrowWidth(e,t,b)+e.Read32(t+0x70);
}
template<class E> void Cache(E&e,U t){
 U x=e.Read32(t+0x90),y=e.Read32(t+0x80);
 for(U i=0;int(i)<int(e.Read32(t+0x84));++i){
  U b=e.Read32(t+0x30)+i*28,state=e.Read8(b+8);if(state&8)continue;
  U stored_x=((state&32)&&(e.Read8(b+9)&1))?e.Read32(t+0x90):x;
  e.Write32(b+20,stored_x);e.Write32(b+24,y);
  if(e.Read8(t+0x24)&4){
   if(state&32){x+=e.Read32(t+0x70)+e.BorderX();y=0;}
   else if(e.Read8(b+9)&1)y+=SeparatorHeight(e,t,b);else y+=e.Read32(t+0x74);
  }else{
   x+=Width(e,t,b,0);
   if(e.Read8(b+8)&32){
    U height=e.Read32(t+0x74);y+=height;
    if(e.Read8(b+9)&1){if(e.Read8(t+8)&128)y-=height;y+=SeparatorHeight(e,t,b);}
    x=e.Read32(t+0x90);
   }
  }
 }
}
// Publish cached layout values lazily.
template<class E> U Height(E&e,U t,U b){if((e.Read8(b+8)&32)&&(e.Read8(b+9)&1)){U h=SeparatorHeight(e,t,b);return (e.Read8(t+8)&128)?h:h+e.Read32(t+0x74);}return e.Read32(t+0x74);}
template<class E> U Rect(E&e,U t,U index,U out){
 if(index>=e.Read32(t+0x84)||(e.Read8(e.Read32(t+0x30)+index*28+8)&8))return 0;
 if(!(e.Read8(t+0xee)&1)){Cache(e,t);e.Write8(t+0xee,e.Read8(t+0xee)|1);}
 U b=e.Read32(t+0x30)+index*28;e.Write32(out,e.Read32(b+20));U width=Width(e,t,b,0);e.Write32(out+8,e.Read32(out)+width);
 b=e.Read32(t+0x30)+index*28;e.Write32(out+4,e.Read32(b+24));U height=Height(e,t,b);e.Write32(out+12,e.Read32(out+4)+height);return 1;
}
// A one-bit scratch bitmap grows by eight.
template<class E> U EnsureBacking(E&e,U t,U width,U height){
 U old=e.Read32(t+0x2c);
 if(old){typename E::Scratch bitmap(e,24);e.BitmapInfo(old,bitmap.address);if(int(width)<=int(e.Read32(bitmap.address+4))&&int(height)<=int(e.Read32(bitmap.address+8)))return 1;}
 U next=e.CreateMono(width+8,height+8);if(!next)return 0;
 old=e.Read32(t+0x2c);if(old)e.DeleteBitmap(old);e.Write32(t+0x2c,next);return 1;
}
// /: negotiated button dimensions and backing growth.
template<class E> U SetSize(E&e,U t,U width,U height,U flags){
 if(!width)width=24;if(!height)height=22;bool padded=flags&1;
 if(padded){
  if(e.Read8(t+9)&16)width+=e.Read32(t+0x9c)+e.Read32(t+0x68);
  width+=e.Read32(t+0x94);height+=e.Read32(t+0x98);
  if(int(width)<int(e.Read32(t+0x70))&&e.Read32(t+0x5c)&&(int(e.Read32(t+0x1c))<5||int(e.Read32(t+0x60))>0))width=e.Read32(t+0x70);
 }else{
  if(height==0xffffffff)height=e.Read32(t+0x74);if(width==0xffffffff)width=e.Read32(t+0x70);
  U minimum=e.Read32(t+0x98)+e.Read32(t+0x6c);if(int(height)<int(minimum))height=minimum;
  minimum=e.Read32(t+0x94)+e.Read32(t+0x68);if(int(width)<int(minimum))width=minimum;
 }
 if(!e.Read32(t+0x2c)||int(width)>int(e.Read32(t+0x70))||int(height)>int(e.Read32(t+0x74)))if(!e.EnsureBacking(t,width,height))return 0;
 if(!(flags&2)){
  if(!padded&&(e.Read32(t+0x70)!=width||e.Read32(t+0x74)!=height))e.Invalidate(e.Read32(t),0,1);
  e.Write32(t+0x70,width);e.Write32(t+0x74,height);e.Write32(t+0x80,(e.Read8(t+9)&8)?0:2);Win98Toolbar::Dirty(e,t);
 }
 return 1;
}

}
