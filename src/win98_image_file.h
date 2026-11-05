// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_image_directory.h"
// USER directory preparation. File IO and allocation are providers.
namespace Win98ImageFile {
using Word=uint16_t;
inline void Put16(unsigned char*p,Word x){p[0]=x;p[1]=x>>8;}
inline unsigned Long(const unsigned char*p){return Win98ImageDirectory::Read(p)|unsigned(Win98ImageDirectory::Read(p+2))<<16;}
inline void MakeEntry(unsigned char*out,const unsigned char*file,Word index,bool icon,Word planes,Word depth){ //
    if(icon){out[0]=file[0];out[1]=file[1];out[2]=file[2];}
    else{Put16(out,file[0]);Put16(out+2,file[1]);}
    Put16(out+4,uint8_t(planes));Put16(out+6,depth);
    for(unsigned i=0;i<4;++i)out[8+i]=file[8+i];Put16(out+12,index);
}
inline Word Select(const Win98ImageDirectory::Display&display,const unsigned char*file,Word count,bool icon,Word cx,Word cy,Word flags,Word planes,Word depth,unsigned char*temporary){
    Put16(temporary,0);Put16(temporary+2,icon?1:2);Put16(temporary+4,count);
    for(Word i=0;i<count;++i)MakeEntry(temporary+6+unsigned(i)*14,file+6+unsigned(i)*16,i,icon,planes,depth);
    return Win98ImageDirectory::Lookup(display,temporary,icon,cx,cy,flags);
}
} // namespace Win98ImageFile
