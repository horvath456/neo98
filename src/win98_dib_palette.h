// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_gdi_palette.h"
#include "win98_dib_color.h"
namespace Win98DibPalette {
using U=unsigned;
// GDI, non-RC_PALETTE color-table constructor. The native
// descriptor/selection providers supply depth, requested count and original entries.
template<class E> U Construct(E&e,Win98GdiPalette::Palette&p,U depth,U requested,U&used,bool indices){
 if(depth>8)return 0;
 U count=requested?requested:p.count,capacity=1u<<depth;
 if(count>=capacity){count=capacity;used=0;}
 else if(used<count)used=count;
 for(U i=0;i<count;++i){
  U index=indices?e.Index(i):i,color=0;
  if(index<p.count){
   if(!indices)p.entries[index].mapping[1]=static_cast<unsigned short>(i);
   color=p.entries[index].color;
   if(color&0x02000000)color=Win98GdiPalette::Vga[color&15];
  }
  e.Color(i,Win98DibColor::Swap(color));
 }
 return count;
}
// GDI, transfer DIB_PAL_COLORS to RGBQUAD conversion. Invalid
// indices select entry zero here (the constructor above emits black instead).
// Its PC_EXPLICIT branch reads only VGA red/green, exactly as the original.
template<class E> void Transfer(E&e,const Win98GdiPalette::Palette&p,U count){
 for(U i=0;i<count;++i){U index=e.Index(i);if(index>=p.count)index=0;
  U color=p.entries[index].color;
  if(color&0x02000000)color=Win98GdiPalette::Vga[color&15]&0xffff;
  e.Color(i,Win98DibColor::Swap(color));
 }
}
// DIBENG. The complete tagged branch bypasses color matching.
inline U DirectColor(U color,U&physical){physical=color|0xffff0000;return physical;}
}
