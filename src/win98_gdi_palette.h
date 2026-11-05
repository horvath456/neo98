// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Native GDI2 logical palette entries and GDI tagged COLORREF.
// Segmented allocation/device instances are native providers, not this kernel.
namespace Win98GdiPalette {
using U = unsigned;
struct Entry { unsigned short mapping[3]{}; U color=0; };
// realized indicates that device-slot mappings exist for DIB_PAL_COLORS
// translation. state becomes 2 after device realization; time records the
// system-palette generation at the last background realization.
struct Palette { Entry* entries=nullptr; U count=0; bool stock=false,dirty=false,realized=false; unsigned char state=0; unsigned short time=0; };
inline bool CreateValid(U version,U count){return version==0x300&&count&&count<=4096;}
inline U Normalize(U color){return color&0x02000000?color&0xffff00ff:color;}
inline U Range(U size,U start,U count){return start>=size?0:count<size-start?count:size-start;}
// GDI. Null output requests the total count regardless of start/count.
inline U Get(const Palette&p,U start,U count,U*out){
 if(!out)return p.count;count=Range(p.count,start,count);
 for(U i=0;i<count;++i)out[i]=p.entries[start+i].color;return count;
}
// GDI. Native provider excludes wrapped/zero-count memory-corruption paths.
inline U Set(Palette&p,U start,U count,U*input){
 if(p.stock)return 0;count=Range(p.count,start,count);if(!count)return 0;p.dirty=true;
 for(U i=0;i<count;++i){input[i]=Normalize(input[i]);p.entries[start+i]=Entry{};p.entries[start+i].color=input[i];}return count;
}
// GDI. Allocation provider must preserve old storage on failure, zero growth.
template<class E> U Resize(E&e,Palette&p,U count){
 if(p.stock)return 0;p.dirty=true;if(count==p.count)return 1;
 if(!e.Resize(p,count))return 0;p.count=count;return 1;
}
// GDI and 0E7F. Logical RGB entries do not exclude PC_* flags.
inline U Nearest(const Palette&p,U color){
 if((color>>24)==1)return color&65535;if(!p.count)return ~0u;
 U best=~0u,index=0;for(U i=0;i<p.count;++i){U c=p.entries[i].color;
  int r=int(c&255)-int(color&255),g=int((c>>8)&255)-int((color>>8)&255),b=int((c>>16)&255)-int((color>>16)&255);
  U d=U(r*r+g*g+b*b);if(!d)return i;if(d<best){best=d;index=i;}
 }return index;
}
// GDI non-hardware update; GDI32: forces public BOOL 1.
inline void Animate(Palette&p,U start,U count,const U*input){
 if(p.stock)return;count=Range(p.count,start,count);
 for(U i=0;i<count;++i)if(p.entries[start+i].color&0x01000000)p.entries[start+i].color=input[i];
}
constexpr U Vga[16]={0,0x80,0x8000,0x8080,0x800000,0x800080,0x808000,0xc0c0c0,0x808080,0xff,0xff00,0xffff,0xff0000,0xff00ff,0xffff00,0xffffff};
// GDI, complete non-RC_PALETTE branch. Original indexed-DIB
// PC_EXPLICIT returns an encoded physical index; hardware realization is separate.
inline U Tagged(const Palette&p,U color,bool indexed_dib){
 U tag=color>>24;if(!tag||tag==0x10)return color;
 if(tag!=1)return color&0xffffff;
 U index=color&65535;if(index>=p.count)index=0;
 if(!p.count)return ~0u;U entry=p.entries[index].color;
 if(entry&0x02000000)return indexed_dib?0xffff0000|(entry&255):Vga[entry&15];
 return entry&0xffffff;
}
// GDI object selection reference decisions (low flag bits retained).
inline U AddReference(U value){return value>65531?value:value+4;}
inline U RemoveReference(U value){return value<4?value:value-4;}

}
