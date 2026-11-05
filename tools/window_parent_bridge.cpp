// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_window_parent.h"
using namespace Win98WindowParent;
using ServiceFn=unsigned(*)(unsigned,unsigned,unsigned,unsigned,unsigned);
struct Bridge:Environment {
 Node*nodes;ServiceFn service;
 Node Read(unsigned h)const override{return h<16?nodes[h]:Node{};}
 void Write(unsigned h,const Node&w)override{if(h<16)nodes[h]=w;}
 unsigned Service(unsigned op,unsigned h,unsigned a,unsigned b,unsigned c)override{return service(op,h,a,b,c);}
};
extern "C" unsigned parent_run(Node*n,ServiceFn s,unsigned desktop,unsigned special,unsigned accessibility,unsigned h,unsigned parent){Bridge e;e.nodes=n;e.service=s;e.desktop=desktop;e.special=special;e.accessibility=accessibility;return SetParent(e,h,parent);}
extern "C" unsigned parent_decision(unsigned op,unsigned a,unsigned b,unsigned c){Position p=Raise(a);if(op==0)return p.flags;if(op==1){p.flags=b;return ActivationCandidate(p);}if(op==2){p.flags=a;return Changing(p);}if(op==3){p.flags=a;return Changed(p);}return ActivationKind(a,b,c,true);}
extern "C" void parent_raise(unsigned h,Win98WindowParent::Position*p){*p=Raise(h);}
