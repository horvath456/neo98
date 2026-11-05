// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_toolbar.h"
namespace Win98ToolbarBitmap {
using U=unsigned;
// COMCTL32. The standard-strip
// resource selection is a provider; ordinary module/bitmap identities are exact.
template<class E> U Add(E&e,U t,U count,U module,U resource){
 if(module==~0u){e.BitmapSize(t,(resource&1)?24:16,(resource&1)?24:16);e.Standard(module,resource,count);}
 U table=e.Read32(t+0x54),groups=e.Read32(t+0x50),offset=0;
 for(U i=0;table&&int(i)<int(groups);++i){U p=table+i*12;
  if(e.Read32(p+4)==module&&e.Read32(p+8)==resource){
   if(int(e.Read32(p))>=int(count))return offset;
   if(i+1==groups){e.Write32(p,count);return offset;}
  }
  offset+=e.Read32(p);
 }
 U next=Win98Toolbar::ResizeAllocation(e,table,(groups+1)*12);if(!next)return ~0u;
 e.Write32(t+0x54,next);U p=next+groups*12;e.Write32(p,count);e.Write32(p+4,module);e.Write32(p+8,resource);
 if(!e.LoadStrip(t,p))return ~0u;e.Write32(t+0x50,groups+1);
 offset=0;for(U i=0;i<groups;++i)offset+=e.Read32(next+i*12);return offset;
}
// default CreateMappedBitmap table.
inline U ColorIndex(U color){const U pairs[]={0,18,0x808080,16,0xc0c0c0,15,0xffffff,20,0xff0000,13,0xff00ff,5};for(U i=0;i<12;i+=2)if(color==pairs[i])return pairs[i+1];return ~0u;}
// Windows extends short strips;
// count zero limits the copy to one image, positive count is a minimum.
inline void Dimensions(U cellw,U cellh,U count,U&width,U&height){
 if(int(width)<int(cellw))width=cellw;if(int(height)<int(cellh))height=cellh;
 if(!count)width=cellw;else if(int(count)>int(width/cellw))width=count*cellw;
}
} // namespace Win98ToolbarBitmap
