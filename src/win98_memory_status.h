// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98MemoryStatus {
using U=unsigned;
// Counters retain the Windows memory categories.
// They are not inferred host operating-system counters.
struct Counters{U physical=0,free_physical=0,physical_subtract=0;U field18=0,field1c=0,field20=0,field24=0,field28=0;U commit0=0,commit1=0,paging_mask=0;bool paging=false;};
// Demand-page information uses the guest counters. FreeArena sums the
// linked free blocks supplied by the native arena provider.
template<class E> void Demand(E&e,const Counters&s,U*out){
 out[0]=0x100000;out[1]=s.physical;out[2]=s.free_physical;out[3]=s.physical-s.physical_subtract;out[4]=0;out[5]=e.FreeArena(0x400);
 out[6]=s.field18;out[7]=s.field1c;out[8]=s.field20;out[9]=s.field24;out[10]=s.field28;out[11]=out[12]=0;
 if(s.paging){out[11]=e.PagingPages();U available=s.physical>=s.commit1?s.physical-s.commit1:0;available&=s.paging_mask;out[12]=s.commit0>=available?s.commit0-available:0;}
 out[13]=s.commit0+s.commit1;
}
// KERNEL32. Original DWORD arithmetic, 2GiB clamps,
// load heuristic and output order. The original division requires physical>0.
inline bool Status(U*info,U*out){
 if(info[1]>=0x80000)info[1]=0x7ffff;if(info[2]>=0x80000)info[2]=0x7ffff;
 if(info[11]+info[1]>=0x80000){info[11]=0x7ffff-info[1];if(info[11]<info[12])info[12]=info[11];}
 if(!info[1])return false;
 U load=(info[13]*50u)/info[1];out[1]=load>=100?100:load;out[2]=info[1]<<12;out[3]=info[2]<<12;
 out[4]=info[11]<<12;out[5]=(info[11]-info[12])<<12;out[6]=0x7fc00000;out[7]=info[5]<<12;out[0]=32;return true;
}
}
