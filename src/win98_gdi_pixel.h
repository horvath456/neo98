// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_dib_color.h"
#include "win98_dib_palette.h"
namespace Win98GdiPixel {
using U=unsigned;struct Point{int x,y;};
struct State{U marker=0,signature=0x4f4d,flags=0,color_flags=0,dirty_flags=0,palette_device=0,palette=0,palette_flags=0,palette_mode=0;};
inline bool Managed(U flags){return (flags&1)&&!(flags&0x280);}
// GDI. Tagged COLORREF, ICM and driver realization are providers.
template<class E>U Resolve(E&e,const State&s,U color,U&physical){
 U original=color;bool mapped=false;
 if(!(s.color_flags&8)&&(color&0xff000000)){color=e.Tagged(color,original);mapped=(color>>24)!=0;}
 if(!mapped&&Managed(s.color_flags)){
  if(s.color_flags&0x40)return e.ManagedColor(color,physical);
  color=e.ForwardColor(color);
 }
 U before=1;if(s.flags&1)before=e.PrepareDevice();
 U result=e.ColorInfo(color,physical,true);if(!before)e.RestoreDevice();
 return (result>>24)==255?original:result;
}
// Complete SetPixel state body GDI; DC validation is external.
template<class E>U Set(E&e,const State&s,Point p,U color){
 if(s.marker==255)return 0;if((signed char)s.signature>=0x50)return e.Record(0x41f);
 if(s.flags&4)e.Update();p=e.Map({short(p.x),short(p.y)});
 if(!e.Clip(p))return ~0u;U physical=0,result=Resolve(e,s,color,physical);e.Pixel(p,physical,true);
 if(s.dirty_flags&10)e.Dirty(p);return result;
}
// GDI. Palette tables and ICM reverse transform are providers.
template<class E>U Get(E&e,const State&s,Point p){
 if(s.flags&4)e.Update();p=e.Map({short(p.x),short(p.y)});if(!e.Clip(p))return ~0u;
 U raw=e.Pixel(p,0,false),unused=0,result;
 if(!s.palette_device)result=e.ColorInfo(raw,unused,false);
 else{
  bool index=s.palette!=0x101&&((s.palette_flags&0x10)||s.palette_mode==2);
  if(!index){raw=e.PaletteColor(raw);if(raw==0xc0dcc0)raw=8;else if(raw==0xf0caa6)raw=9;else if(raw==0xf0fbff)raw=0xf6;else if(!(raw&0xff000000)){result=raw;goto done;}}
  raw&=255;if(s.flags&1){U value=0;if(e.LogicalColor(raw,value)){if(!(value&0x02000000))return value&0xffffff;raw=value&65535;}}
  result=e.SystemColor(raw)&0xffffff;
 }
 done:if(Managed(s.color_flags))result=e.ReverseColor(result);return result;
}
// GDI and GDI32:.
template<class E>U Nearest(E&e,const State&s,U color){U physical=0,result=Resolve(e,s,color,physical);if(!(s.color_flags&8)&&(result>>24)==255){result=e.SystemColor(result&255)&0xffffff;if((color>>24)==0x20)result|=0x20000000;}return result;}
inline U SetV(U result){return result!=~0u;}
// DIBENG. Palette DWORDs are BGR0; flagged entries are excluded.
template<class E>U NearestIndex(E&e,U count,U color){
 U best=~0u,index=count;bool reserved=count==256&&(e.Palette(8)&0x80000000);
 for(U i=0;i<count;++i){if(reserved&&i==8)i=248;U entry=e.Palette(i);if(entry&0x80000000)continue;
  int r=int((entry>>16)&255)-int(color&255),g=int((entry>>8)&255)-int((color>>8)&255),b=int(entry&255)-int((color>>16)&255);U distance=U(r*r+g*g+b*b);
  if(!distance)return i;if(distance<best){best=distance;index=i;}
 }return index;
}
// Driver physical pixel path DIBENG. Storage and device hooks are providers.
inline U Rop(U mode,U p,U d){switch((mode-1)&15){case 0:return 0;case 1:return ~(p|d);case 2:return d&~p;case 3:return ~p;case 4:return p&~d;case 5:return ~d;case 6:return p^d;case 7:return ~(p&d);case 8:return p&d;case 9:return ~(p^d);case 10:return d;case 11:return d|~p;case 12:return p;case 13:return p|~d;case 14:return p|d;default:return ~0u;}}
template<class E>U DriverPixel(E&e,U width,U height,U depth,U flags,Point p,U color,U mode,bool write){
 U x=(unsigned short)p.x,y=(unsigned short)p.y;if(x>=width||y>=height||(flags&0x10))return 0x80000000;
 if(flags)e.Begin(p);U value=e.Read(x,y);
 if(write){e.Write(x,y,Rop(mode,color,value));value=0;}
 else if(depth==1)value|=0xffff0000;else if(depth!=32)value|=0xff000000;
 if(flags)e.End();return value;
}
// Original DIBENG3 color conversion helpers and nearest-palette scan.
template<class E>U DriverColor(E&e,U depth,bool rgb565,U color,U&physical,bool write,bool monochrome=false){
 if(!write){if(monochrome)return color&1?0xffffff:0;return Win98DibColor::ToColor(e,0,depth,rgb565,color);}
 if(color&0xff000000)return Win98DibPalette::DirectColor(color,physical);
 if(monochrome){U rb=((color&255)+((color>>16)&255))/2;U v=(rb+((color>>8)&255))/2>=127;physical=((0x80|v)<<24)|v;return v?0xffffff:0;}
 if(depth<=8){U index=NearestIndex(e,e.Count(),color);physical=index;return Win98DibColor::Swap(e.Palette(index));}
 if(depth==16){physical=Win98DibColor::FromColor16(color,rgb565);return Win98DibColor::ToColor(e,0,depth,rgb565,physical);}
 physical=Win98DibColor::Swap(color);if(depth==24&&(color&255)==((color>>8)&255)&&(color&255)==((color>>16)&255))physical|=0x40000000;return color&0xffffff;
}
}
