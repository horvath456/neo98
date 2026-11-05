// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_property_sheet_modal.h"
#include <cstring>
#include <vector>
using U=unsigned;constexpr U BASE=0x20000000,S=BASE+0x100;
struct E{unsigned char*m;U*row;std::vector<U>trace;
 U Read(U p){U v;std::memcpy(&v,m+p-BASE,4);return v;}void Write(U p,U v){std::memcpy(m+p-BASE,&v,4);}
 bool Alive(){return true;}U PageWindow(U i){return row[4]&(1u<<i)?100+i:0;}
 U TabPage(U,U i){return PageWindow(i);}
 U Notify(U target,U,U code,U parameter){U i=target?target-100:~0u;trace.insert(trace.end(),{1,i,code,parameter});if(code==~200u)return row[3];return i==row[1]?row[2]:0;}
 void SetChanged(U i,bool){Write(BASE+0x400+i*4,Read(BASE+0x400+i*4)&~1u);}
 void Select(U i){Write(BASE+0x500,i);trace.insert(trace.end(),{2,i,0,0});}
 void Applied(){Write(S+0x44,1);trace.insert(trace.end(),{3,0,0,0});}
 void ActivateCurrent(U){trace.insert(trace.end(),{4,0,0,0});}
};
extern "C" void sheet_modal(U*row,unsigned char*memory,U*out){E e{memory,row};out[0]=Win98PropertySheet::Apply(e,S,row[0]);out[1]=e.trace.size();std::memcpy(out+2,e.trace.data(),e.trace.size()*4);}
