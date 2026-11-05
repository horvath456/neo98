// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_bitmap.h"
// Original GDI compatible-bitmap constructor and stretch state decisions.
namespace Win98DibStretchObjects {
using U=unsigned;
inline U Word(U value){return value&65535;}
// GDI. The original DWORD product permits a WORD byte stride.
template<class E> U DeviceBitmap(E &e,U dc,U width,U height,U planes,U depth,U bits=0,U flags=0){
 width=Word(width);height=Word(height);planes&=255;depth&=255;
 const U aligned=width*depth+15;if(aligned>>19)return 0;
 return e.AllocateBitmap(Win98Bitmap::Bitmap{0,width,height,(aligned>>3)&65534,planes,depth,bits},dc,flags&255);
}
// GDI. DC validation belongs to the public entry provider.
template<class E> U Compatible(E &e,U dc,U width,U height,bool discardable=false){
 dc=Word(dc);width=Word(width);height=Word(height);
 const U object=e.DcObject(dc);
 if(e.Read8(object+15)&0x80){
  const U info=e.DibHeader(dc),old_width=e.Read32(info+4),old_height=e.Read32(info+8);
  e.Write32(info+4,width);e.Write32(info+8,(old_width&0xffff0000u)|height);
  const U result=e.CreateSection(dc,info);
  e.Write32(info+8,old_height);e.Write32(info+4,old_width);
  return result;
 }
 const U result=DeviceBitmap(e,dc,width,height,e.Read8(object+0x9c),e.Read8(object+0x9d));
 if(result&&discardable){
  const U bitmap=e.Object(result);e.MakeDiscardable(e.Read16(bitmap+14));
  if(e.Read16(bitmap+30)&1)e.MakeDiscardable(e.Read16(bitmap+20));
 }
 return result;
}
// GDI validates a signed WORD in [1,4], then exchanges
// the ordinary DC WORD+72h. Handle/COW/metafile work remains a provider.
inline U SetMode(U &current,U value){value=Word(value);if(!value||value>4)return 0;const U before=Word(current);current=value;return before;}
// GDI chooses this mode for its software fallback.
inline U SoftwareMode(U value){value=Word(value);return value==4?3:value>=1&&value<=3?value:2;}
struct Rect {int left,top,right,bottom;};
inline int SignedWord(U value){return int(value&32767)-int(value&32768);}
// GDI. Reversed extents include the origin pixel, hence +1.
inline U Normalize(Rect &r,U flags){
 int left=SignedWord(U(r.left)),right=SignedWord(U(r.right)),top=SignedWord(U(r.top)),bottom=SignedWord(U(r.bottom));
 if(left>right){const int old=left;left=SignedWord(U(right+1));right=SignedWord(U(old+1));flags^=8;}
 if(top>bottom){const int old=top;top=SignedWord(U(bottom+1));bottom=SignedWord(U(old+1));flags^=4;}
 r={left,top,right,bottom};return flags&255;
}
}
