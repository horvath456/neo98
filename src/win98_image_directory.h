// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
// USER.EXE and 2874..28CB. The caller supplies a validated
// resource-directory buffer and the display's original USER metric fields.
namespace Win98ImageDirectory {
using Word=uint16_t;
struct Display {Word icon_width,icon_height,cursor_width,cursor_height,flags,depth;};
inline Word Read(const unsigned char*p){return Word(p[0]|unsigned(p[1])<<8);}
inline Word Dimension(const Display&d,Word requested,Word icon,Word flags,Word fallback,bool vertical){
    if(requested)return requested;
    if(!(flags&0x40))return fallback;
    return icon?(vertical?d.icon_height:d.icon_width):(vertical?d.cursor_height:d.cursor_width);
}
inline Word ColorRank(Word colors){ // signed comparisons retained
    const int n=int16_t(colors);
    return n<4?1:n<8?2:n<16?3:n<256?4:8;
}
inline Word Difference(Word a,Word b){ // undersized images cost twice
    const Word delta=Word(a-b);
    return int16_t(delta)<0?Word(0-Word(delta+delta)):delta;
}
inline Word Score(const unsigned char*entry,Word&cx,Word&cy,Word colors,bool cursor){
    Word w=cursor?Read(entry):entry[0],h=cursor?Word(Read(entry+2)>>1):entry[1];
    Word entry_colors=cursor?2:entry[2];
    if(!w)w=256;if(!cx)cx=w;if(!h)h=256;if(!cy)cy=h;if(!entry_colors)entry_colors=256;
    const Word color=Word(Difference(ColorRank(colors),ColorRank(entry_colors))*2);
    return Word(color+Difference(w,cx)+Difference(h,cy));
}
inline Word Select(const unsigned char*entries,Word count,Word cx,Word cy,Word depth,bool cursor){
    Word best=0,score=0xffff;
    const Word colors=Word(1u<<(depth>8?8:depth));
    for(Word i=0;i<count;++i){const Word value=Score(entries+unsigned(i)*14,cx,cy,colors,cursor);
        if(!value)return i;if(value<score){score=value;best=i;}}
    return best;
}
inline Word Lookup(const Display&d,const unsigned char*directory,Word icon,Word cx,Word cy,Word flags){
    cx=Dimension(d,cx,icon,flags,0,false);cy=Dimension(d,cy,icon,flags,0,true);
    if(!icon && !(d.flags&1))flags|=1;
    const Word depth=(flags&1)?1:((flags&0x80)||((d.flags&0x40)&&(d.flags&0x20)))?4:d.depth;
    const Word count=Read(directory+4);
    Word index=Select(directory+6,count,cx,cy,depth,icon==0);
    if(index==count)index=0;
    return Read(directory+6+unsigned(index)*14+12);
}
} // namespace Win98ImageDirectory
