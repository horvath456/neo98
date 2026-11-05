// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_toolbar_layout.h"
#include <cassert>
#include <cstring>
#include <cstdio>
using U=unsigned;static unsigned char memory[65536];static U config,events[4096],event_count;static U(*outside_read)(U);static void(*outside_write)(U,U);
struct E{
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
#ifdef TOOLBAR_LAYOUT_STANDALONE
U word(FILE*f){U n;assert(fread(&n,4,1,f)==1);return n;}void delta(FILE*f,unsigned char*m){for(U n=word(f);n;--n){U p=word(f),bytes=word(f);assert(p<=65536&&bytes<=65536-p);assert(fread(m+p,1,bytes,f)==bytes);}}
int main(int argc,char**argv){assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);U total=word(f);unsigned char before[65536],after[65536];for(U n=0;n<total;++n){U op=word(f),config=word(f),expected=word(f),a[4];assert(fread(a,4,4,f)==4);memset(before,0xa5,sizeof before);delta(f,before);memcpy(after,before,sizeof after);delta(f,after);U count=word(f),want[4096],got[4096];assert(count<=4096&&fread(want,4,count,f)==count);layout_reset(before,config);U result=layout_run(op,a);assert(op==5||result==expected);assert(!memcmp(memory,after,sizeof after));assert(layout_events(got)==count&&!memcmp(want,got,count*4));}puts("TOOLBAR LAYOUT ASAN/UBSAN PASS");}
#endif
