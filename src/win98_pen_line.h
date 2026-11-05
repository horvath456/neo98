// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Native decisions from original GDI1 pen records and DIBENG7/13 lines.
namespace Win98PenLine {
using U=unsigned;
inline int Word(U v){return int(v&32767)-int(v&32768);}
struct Pen {U style,width_x,width_y,color,flags;};
// GDI build the same twelve-byte record; 7FEA allocates type4F47.
template<class E> U Create(E&e,const Pen&p){return e.AllocatePen({p.style&65535,p.width_x&65535,p.width_y&65535,p.color,0x22});}
template<class E> U CreateDirect(E&e,U style,U width,U color){style&=65535;if(style>6)return 0;return Create(e,{style,width,0,color,0x22});}
// GDI. The native provider separately validates guest LOGBRUSH.
template<class E> U CreateExtended(E&e,U style,U width,U brush_style,U color){
 if((style&0x10000)&&width>0x4000)return 0;
 if(brush_style||((style&255)>6))return 0;
 U flags=style&0x100?4:style&0x200?1:2;
 flags|=style&0x1000?8:style&0x2000?16:32;
 return e.AllocatePen({style&255,(style&0x10000)?width&65535:0,(style&0x10000)?width&65535:0,color,flags});
}
// GDI keeps the low reference flags and cancels WORD wrap.
inline U AddReference(U value){value&=65535;return value<=65531?value+4:value;}
inline U RemoveReference(U value){value&=65535;return value>=4?value-4:value;}
// GDI realizes the new selection before committing references.
template<class E> U Select(E&e,U &current,U handle){const U previous=current;current=handle;if(!e.Realize(handle)){current=previous;return 0;}e.AddReference(handle);if(previous)e.RemoveReference(previous);return previous?previous:1;}
// GDI returns success without freeing protected/referenced pens.
template<class E> U Delete(E&e,U references,U attributes){if(attributes&0xa000)return 1;if(references&0xfffc){e.Diagnostic();return 1;}e.Free();return 1;}
// GDI validates a signed WORD, then 7B62/7B08 exchanges DC+4Eh.
template<class T> U SetRop(T &current,U value){value&=65535;if(value<1||value>16)return 0;U before=U(current)&65535;current=value;return before;}
// GDI and shared1DFD..1E50 expands the ten-byte LOGPEN.
template<class E> U Query(E &e,const Pen&p,U count,U output){
 if(!output)return 16;
 const U values[]={U(Word(p.style)),U(Word(p.width_x)),U(Word(p.width_y)),p.color};
 const U bytes=count<16?count:16;
 for(U i=0;i<bytes;++i)if(!e.Write8(output+i,(values[i/4]>>(8*(i%4)))&255))return 0;
 if(!bytes)e.Error(87);return bytes;
}
// DIBENG: wide pens become solid; NULL remains NULL.
inline U RealizedStyle(const Pen&p){if(p.style==5)return 5;return p.width_x<=1&&p.style<=5?p.style:0;}
// DIBENG table; low/high bytes encode opposite drawing directions.
inline U Dash(U style){constexpr U patterns[]={0xffff,0xe7e7,0x55aa,0x27e4,0x57ea};return patterns[style<5?style:0];}
inline U Rop(U mode,U p,U d){switch(mode){case 1:return 0;case 2:return ~(p|d);case 3:return d&~p;case 4:return ~p;case 5:return p&~d;case 6:return ~d;case 7:return p^d;case 8:return ~(p&d);case 9:return p&d;case 10:return ~(p^d);case 11:return d;case 12:return d|~p;case 13:return p;case 14:return p|~d;case 15:return p|d;case 16:return 0xffffff;default:return d;}}
struct Point {int x,y;};
inline Point Move(Point &current,Point next){const Point before=current;current={Word(U(next.x)),Word(U(next.y))};return before;}
template<class E> U Line(E &e,Point &current,Point next){const Point before=Move(current,next);const Point points[]={before,current};e.Draw(points,2);return 1;}
struct Rect {int left,top,right,bottom;};
// DIBENG7 octant normalization and endpoint exclusion, expressed with the
// closed form of its integer error recurrence. Native pixel storage is external.
template<class E> bool Raster(E&e,const Point*points,U count,U style,U rop,U color,U background,U background_mode,Rect clip){
 if(style>=5)return false;
 U phase=0;
 for(U n=1;n<count;++n){
  Point a=points[n-1],b=points[n];bool reverse=a.x>b.x;
  if(reverse){Point t=a;a=b;b=t;}
  const int dx=b.x-a.x,dy=b.y-a.y,ady=dy<0?-dy:dy;
  const int major=dx>ady?dx:ady,minor=dx>ady?ady:dx;
  if(!major)continue;
  int first=reverse?1:0;const int last=major+(reverse?1:0);
  for(;first<last;++first){
   const int step=int((2ull*U(first)*U(minor)+U(major)-(dy>=0?1u:0u))/(2u*U(major)));
   const int x=a.x+(dx>=ady?first:step),y=a.y+(dy<0?-1:1)*(dx>=ady?step:first);
   if(x>=clip.left&&x<clip.right&&y>=clip.top&&y<clip.bottom)break;
  }
  U mask=Dash(style),at=reverse?~(phase+U(major)*64):phase;
  mask=reverse?mask>>8:mask&255;
  const U rotations=(at>>8)&7;mask=((mask<<rotations)|(mask>>(8-rotations)))&255;
  const U initial=(at&255)+U(first)*64,initial_shift=(initial>>8)&7;
  mask=((mask<<initial_shift)|(mask>>(8-initial_shift)))&255;
  for(int i=first;i<last;++i){
   const int step=int((2ull*U(i)*U(minor)+U(major)-(dy>=0?1u:0u))/(2u*U(major)));
   const int x=a.x+(dx>=ady?i:step),y=a.y+(dy<0?-1:1)*(dx>=ady?step:i);
   const U shift=(((initial&255)+U(i-first)*64)>>8)&7;
   const U pattern=((mask>>shift)|(mask<<(8-shift)))&255;
   if(x<clip.left||x>=clip.right||y<clip.top||y>=clip.bottom)continue;
   const bool on=!style||(pattern&128);
   if(on||background_mode==2)e.Pixel(x,y,Rop(rop,on?color:background,e.Read(x,y)));
  }
  phase=(phase+U(major)*64)&65535;
 }
 return true;
}
}
