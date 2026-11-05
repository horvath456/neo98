/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "win98_toolbar_bitmap.h"
#include <cstring>
static unsigned char memory[65536];static unsigned config,next;static unsigned events[128],used;
struct E{
 unsigned Read32(unsigned p){unsigned v;memcpy(&v,memory+p-0x100000,4);return v;}
 void Write32(unsigned p,unsigned v){memcpy(memory+p-0x100000,&v,4);}
 void FreeLocal(unsigned){}
 unsigned AllocateLocal(unsigned f,unsigned n){return Allocate(0,n,f);}
 unsigned ReallocateLocal(unsigned p,unsigned n,unsigned f){return Allocate(p,n,f);}
 unsigned Allocate(unsigned p,unsigned n,unsigned f){events[used++]=1;events[used++]=p;events[used++]=n;events[used++]=f;if(config&1)return 0;unsigned result=next;next+=((n+15)&~15u);if(p)memcpy(memory+result-0x100000,memory+p-0x100000,12*4);return result;}
 unsigned LoadStrip(unsigned t,unsigned p){events[used++]=2;events[used++]=t;events[used++]=p;return !(config&2);}
 void BitmapSize(unsigned,unsigned,unsigned){}void Standard(unsigned&,unsigned&,unsigned&){}
};
extern "C"{
void reset(const unsigned char*p,unsigned c){memcpy(memory,p,65536);config=c;next=0x108000;used=0;}
unsigned char*arena(){return memory;}unsigned trace(unsigned*out){memcpy(out,events,used*4);return used;}
unsigned run(unsigned t,unsigned n,unsigned m,unsigned r){E e;return Win98ToolbarBitmap::Add(e,t,n,m,r);}
unsigned provider(unsigned op,unsigned a,unsigned b,unsigned c){E e;return op==1?e.Allocate(a,b,c):e.LoadStrip(a,b);}
void dimensions(unsigned w,unsigned h,unsigned count,unsigned*out){Win98ToolbarBitmap::Dimensions(w,h,count,out[0],out[1]);}
}
