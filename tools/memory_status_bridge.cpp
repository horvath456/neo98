/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_memory_status.h"
#include <cstring>
extern "C" unsigned memory_status_run(unsigned mode,const unsigned*state,unsigned*info,unsigned*out){
 if(!mode){Win98MemoryStatus::Counters s{};s.physical=state[0];s.free_physical=state[1];s.physical_subtract=state[2];s.field18=state[3];s.field1c=state[4];s.field20=state[5];s.field24=state[6];s.field28=state[7];s.commit0=state[8];s.commit1=state[9];s.paging_mask=state[10];s.paging=state[11];struct E{const unsigned*s;unsigned FreeArena(unsigned){return s[12];}unsigned PagingPages(){return s[13];}}e{state};Win98MemoryStatus::Demand(e,s,info);return 1;}return Win98MemoryStatus::Status(info,out);
}
#ifdef MEMORY_STATUS_SANITIZER
#include <cstdio>
#include <cassert>
int main(){unsigned mode,state[14],info[14],out[8],expected[22],n=0;while(fread(&mode,4,1,stdin)==1){assert(fread(state,4,14,stdin)==14&&fread(info,4,14,stdin)==14&&fread(out,4,8,stdin)==8&&fread(expected,4,22,stdin)==22);assert(memory_status_run(mode,state,info,out));assert(!memcmp(expected,info,sizeof info)&&!memcmp(expected+14,out,sizeof out));++n;}printf("PASS %u original cases ASan/UBSan\n",n);}
#endif
