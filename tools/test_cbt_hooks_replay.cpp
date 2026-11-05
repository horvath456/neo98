// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_cbt_hooks.h"
#include "cbt_hooks_destroy_bridge.cpp"
#include <cassert>
#include <cstdio>
#include <cstring>
int main(int argc,char**argv){assert(argc==3);FILE*f=fopen(argv[1],"rb");assert(f);unsigned c[4],n=0;while(fread(c,sizeof c,1,f)==1){assert((c[0]?Win98CbtHooks::MinMaxRequest(c[1],c[2]):Win98CbtHooks::Veto(c[1],c[2]))==c[3]);++n;}assert(feof(f));fclose(f);FILE*d=fopen(argv[2],"rb");assert(d);unsigned mode,k=0;CbtTree before,want;while(fread(&mode,4,1,d)==1){assert(fread(&before,sizeof before,1,d)==1&&fread(&want,sizeof want,1,d)==1);cbt_destroy(&before,mode);assert(!memcmp(&before,&want,sizeof before));++k;}assert(feof(d));fclose(d);printf("CBT destruction ASan/UBSan PASS: %u original cases\n",k);printf("CBT event ASan/UBSan PASS: %u original cases\n",n);}
