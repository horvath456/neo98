// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
// USER. Pointer readability remains an address-space provider.
namespace Win98ImageProbe {
using U=uint32_t;
inline U Read32(const unsigned char*p){return U(p[0])|U(p[1])<<8|U(p[2])<<16|U(p[3])<<24;}
inline U Read16(const unsigned char*p){return U(p[0])|U(p[1])<<8;}
inline U Pitch(U bits){return ((bits+31)&0xffffffe7u)>>3;}
template<class E> bool Icon(E&e,U declared_size,uint16_t icon,U version){
    if((version>>16)<2||version>0x40000)return false;
    U required=icon?40:44;if(!e.Readable(0,required))return false;
    const U skip=icon?0:4;const auto*p=e.Data()+skip;if(Read32(p)!=40)return false;
    const U colors=Read32(p+32),bpp=Read16(p+14);
    if(colors)required+=colors*4;else if(bpp<=8)required+=U(1)<<(bpp+2);
    const U height=U(int32_t(Read32(p+8))/2),width=Read32(p+4);
    required+=Pitch(width*bpp)*height;required+=Pitch(width)*height;
    return e.Readable(skip,required)&&required<=declared_size;
}
} // namespace Win98ImageProbe
