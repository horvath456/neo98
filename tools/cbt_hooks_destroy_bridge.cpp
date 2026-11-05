// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_cbt_hooks.h"
using U=unsigned;
struct CbtTree {U first[9],next[9],parent[9],flags[9],alive[9],trigger,target,count,trace[256];};
struct E:Win98CbtHooks::DestroyEnvironment{
 CbtTree&s;explicit E(CbtTree&a):s(a){}
 bool Valid(U h)override{return h&&h<9&&s.alive[h];}U Flags(U h)override{return s.flags[h];}
 void Mark(U h,U f)override{s.flags[h]|=f;}U First(U h)override{return s.first[h];}U Next(U h)override{return s.next[h];}
 U DetachFirst(U h)override{U child=s.first[h];if(child)s.first[h]=s.next[child];return child;}
 void Record(U h,U m){s.trace[s.count++]=h;s.trace[s.count++]=m;s.trace[s.count++]=s.first[h];s.trace[s.count++]=s.flags[h];}
 void Remove(U h){if(!Valid(h))return;U p=s.parent[h];if(s.first[p]==h)s.first[p]=s.next[h];else for(U x=s.first[p];x;x=s.next[x])if(s.next[x]==h){s.next[x]=s.next[h];break;}s.alive[h]=0;}
 void Send(U h,U m)override{Record(h,m);if(s.trigger==(h|(m<<8))){Remove(s.target);s.trigger=0;}}
 void ClearFocus(U h)override{Record(h,0x5c02);}
 void Free(U h)override{Record(h,0);s.alive[h]=0;}
};
extern "C" void cbt_destroy(CbtTree*s,unsigned final){E e(*s);if(final)Win98CbtHooks::DestroyNonClient(e,1);else{e.Mark(1,0x20);Win98CbtHooks::DestroyMessages(e,1);}}
