// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_compare_string_general.h"
#include <cassert>
#include <cstdio>
struct E{unsigned reads=0,limit;bool failed=false;bool Aborted()const{return failed;}
 unsigned Read8(unsigned){if(++reads>limit){failed=true;return 0;}return 'a';}
 unsigned Weight(unsigned,unsigned c){return 0x2020e00|c;}unsigned Expansion(unsigned,unsigned,unsigned){return 'a';}
 void Unsupported(const char*){failed=true;}
};
int main(){const unsigned limits[]={0,1,4,16};for(unsigned limit:limits){E e{0,limit};unsigned r=Win98CompareString::General(e,1,0,0x1000,0x7fffffff,0x2000,0x7fffffff);assert(r==0&&e.failed&&e.reads<=limit+2);printf("ABORT limit=%u reads=%u result=%u\n",limit,e.reads,r);}puts("KERNEL COMPARE ABORT PASS");}
