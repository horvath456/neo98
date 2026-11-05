/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_library_ex.h"
#include <cstring>
struct E {unsigned char*m;const unsigned*c;unsigned*out;unsigned n=0,error=777;unsigned Read8(unsigned p){return m[p-0x100000];}void Record(unsigned id,unsigned arg=0){out[4+n++]=id;out[4+n++]=arg;}void Error(unsigned v){error=v;}bool ConvertMode(){Record(1);return c[0];}unsigned Convert(unsigned p){Record(2,p);return c[1]?0:0x100400;}bool CompatibilityMode(){return c[2];}bool Compatibility(unsigned&p,unsigned&own,bool converted){Record(3,p);own=c[6];return !c[3];}unsigned Duplicate(unsigned p){Record(4,p);return c[4]?0:0x100600;}void Free(unsigned p){Record(7,p);}unsigned Normal(unsigned p){Record(5,p);return c[5];}unsigned Datafile(unsigned p){Record(6,p);return c[5];}void FreeConverted(unsigned p){Record(8,p);}void ClearConversionFlags(){m[0x1045]&=0xf9;}};
extern "C" void library_ex_run(const unsigned*a,const unsigned*c,unsigned char*m,unsigned*out){E e{m,c,out};Win98LibraryEx::State s{c[7]};out[0]=Win98LibraryEx::Load(e,s,a[0],a[1],a[2]);out[1]=e.error;out[2]=s.altered;out[3]=e.n;}
#ifdef LIBRARY_EX_SANITIZER
#include <cstdio>
#include <cassert>
int main(){unsigned a[3],c[8],expected[68],actual[68],count=0;unsigned char memory[8192];while(fread(a,4,3,stdin)==3){assert(fread(c,4,8,stdin)==8&&fread(memory,1,sizeof memory,stdin)==sizeof memory&&fread(expected,4,68,stdin)==68);memset(actual,0,sizeof actual);library_ex_run(a,c,memory,actual);assert(!memcmp(expected,actual,sizeof actual));++count;}printf("PASS %u original LoadLibraryExA cases ASan/UBSan\n",count);}
#endif
