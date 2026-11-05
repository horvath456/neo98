// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_toolbar_paint.h"
#include <cassert>
#include <cstring>
#include <cstdio>
using U=unsigned;static unsigned char memory[65536];static U config,events[4096],event_count;static U(*outside_read)(U);static void(*outside_write)(U,U);
struct E{
 U SetSize(U t,U w,U h,U flags){Event(10,t,w,h);Event(11,flags);return !(config&4);}
 U AllocateLocal(U flags,U n){Event(12,flags,n);return config&8?0:0x108000;}
 U ReallocateLocal(U old,U n,U flags){Event(13,old,n,flags);if(config&8)return 0;for(U i=0;i<n;++i)Write8(0x108000+i,Read8(old+i));return 0x108000;}
 void FreeLocal(U p){Event(14,p);}
 bool Clipped(U t,U){Event(15,t);return config&16;}
 unsigned char temporary[32];struct Scratch{U address=0x20000000;Scratch(E&,U n){assert(n<=32);}};
 U Read8(U p){if(p>=0x20000000&&p<0x20000020)return temporary[p-0x20000000];if(p>=0x100000&&p<0x110000)return memory[p-0x100000];assert(outside_read);return outside_read(p);}
 U Read16(U p){return Read8(p)|(Read8(p+1)<<8);}U Read32(U p){return Read16(p)|(Read16(p+2)<<16);}
 void Write8(U p,U v){if(p>=0x20000000&&p<0x20000020)temporary[p-0x20000000]=v;else if(p>=0x100000&&p<0x110000)memory[p-0x100000]=v;else{assert(outside_write);outside_write(p,v);}}
 void Write16(U p,U v){Write8(p,v);Write8(p+1,v>>8);}void Write32(U p,U v){Write16(p,v);Write16(p+2,v>>16);}
 void Event(U a,U b=0,U c=0,U d=0){assert(event_count+4<=4096);events[event_count++]=a;events[event_count++]=b;events[event_count++]=c;events[event_count++]=d;}
 U EnsureBacking(U t,U width,U height){return Win98ToolbarLayout::EnsureBacking(*this,t,width,height);}
 void BitmapInfo(U bitmap,U p){Event(7,bitmap);for(U i=0;i<24;i+=4)Write32(p+i,0);Write32(p+4,32);Write32(p+8,30);}
 U CreateMono(U width,U height){Event(8,width,height);return config&4?0:0x9002;}
 void DeleteBitmap(U bitmap){Event(9,bitmap);}
 void Invalidate(U h,U rect,U erase){Event(6,h,rect,erase);}
 U BorderX(){return 2;}
 U Text(U t,U b){Event(1,t,b);return config&1?0x109000:0;}
 U TextWidth(U t,U b,U dc){Event(2,t,b,dc);return config>>16;}
 U ImagePresent(U t,U b){Event(3,t,b);return (config>>1)&1;}
 void ClientRect(U h,U p){Event(4,h);Write32(p,3);Write32(p+4,5);Write32(p+8,303);Write32(p+12,105);}
};
extern "C" void layout_reset(const unsigned char*p,U c){memcpy(memory,p,sizeof memory);config=c;event_count=0;}
extern "C" const unsigned char*layout_memory(){return memory;}
extern "C" U layout_events(U*out){memcpy(out,events,event_count*4);return event_count;}
extern "C" void layout_callbacks(U(*read)(U),void(*write)(U,U)){outside_read=read;outside_write=write;}
extern "C" U layout_provider(U op,const U*a){E e;switch(op){case 1:return e.Text(a[0],a[1]);case 2:return e.TextWidth(a[0],a[1],a[2]);case 3:return e.ImagePresent(a[0],a[1]);case 4:e.ClientRect(a[0],a[1]);return 1;case 5:return e.EnsureBacking(a[0],a[1],a[2]);case 6:e.Invalidate(a[0],a[1],a[2]);return 1;case 7:e.BitmapInfo(a[0],a[2]);return 24;case 8:assert(a[2]==1&&a[3]==1&&a[4]==0);return e.CreateMono(a[0],a[1]);case 9:e.DeleteBitmap(a[0]);return 1;}assert(false);return 0;}
extern "C" U layout_run(U op,const U*a){E e;using namespace Win98ToolbarLayout;switch(op){case 0:return SeparatorHeight(e,a[0],a[1]);case 1:return HasArrow(e,a[0],a[1]);case 2:return TextArrow(e,a[0],a[1]);case 3:return ArrowWidth(e,a[0],a[1]);case 4:return Width(e,a[0],a[1],a[2]);case 5:Cache(e,a[0]);return 0;case 6:return Height(e,a[0],a[1]);case 7:return Rect(e,a[0],a[1],a[2]);case 8:return SetSize(e,a[0],a[1],a[2],a[3]);case 9:return EnsureBacking(e,a[0],a[1],a[2]);}assert(false);return 0;}

extern "C" U paint_provider(U op,const U*a){E e;switch(op){case 10:return e.SetSize(a[0],a[1],a[2],a[3]);case 12:return e.AllocateLocal(a[0],a[1]);case 13:return e.ReallocateLocal(a[0],a[1],a[2]);case 14:e.FreeLocal(a[0]);return 0;case 15:return e.Clipped(a[0],a[1]);case 16:return Win98ToolbarLayout::Rect(e,a[0],a[1],a[2]);}return layout_provider(op,a);}
extern "C" U paint_run(U op,const U*a){E e;using namespace Win98ToolbarPaint;switch(op){case 0:return BitmapSize(e,a[0],a[1],a[2]);case 1:return TextHeight(e,a[0],a[1]);case 2:return SetImages(e,a[0],a[1],a[2],a[3]);case 3:return Images(e,a[0],a[1],a[2]);case 4:return Text(e,a[0],a[1]);case 5:return Padding(e,a[0],a[1]);case 6:return Rows(e,a[0]);case 7:return Hit(e,a[0],int(a[1]),int(a[2]));case 8:return SplitArrow(e,a[0],a[1]);}assert(false);return 0;}
