// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_selector_table.h"
#include <cassert>
extern "C" void map(unsigned pointer,unsigned offset,unsigned *out){
 Win98Selectors::Table t;out[0]=t.Map(pointer);out[1]=t.Linear(out[0]+offset);
 unsigned char descriptor[8]={};t.Descriptor(out[0]>>16,descriptor);
 for(unsigned i=0;i<8;++i)out[2+i]=descriptor[i];
}
extern "C" void lifetime(){
 Win98Selectors::Table t;
 unsigned a=t.Map(0x12345678),b=t.Map(0x12345678);assert(a!=b);
 assert(t.Linear(a+65535)==0x12355677);t.Unmap(a);assert(!t.Linear(a)&&t.Linear(b)==0x12345678);
 unsigned c=t.Map(0x87654321);assert(c==a&&t.Linear(c)==0x87654321);
 assert(!t.Linear(0x00080000)&&!t.Linear(0xffff0000)&&t.Linear(42)==42);
 unsigned arena=t.Map(0x700000,true);for(unsigned i=0;i<256;++i)t.Fix(arena);
 assert(t.Find(arena)->locks==255);t.Unfix(arena);assert(t.Find(arena)->locks==255);
 for(unsigned i=3;i<4095;++i)assert(t.Map(0x100000+i));assert(!t.Map(0x100000));
}
