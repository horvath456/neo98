// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_window_hit.h"
using namespace Win98WindowHit;
struct State{Window w[12];U valid,task,modal,denied,regions;int hits[12];U mutation,trace[256],count;};
struct Provider{
 State&s;void Event(U a,U b,U c=0,U d=0){s.trace[s.count++]=a;s.trace[s.count++]=b;s.trace[s.count++]=c;s.trace[s.count++]=d;}
 Window Read(U h){return h<12?s.w[h]:Window{};}bool Valid(U h){return h&&h<12&&(s.valid&(1u<<h));}U Desktop(){return 1;}
 bool RegionContains(U r,Point p){Event(1,r,U(p.x),U(p.y));return s.regions&(1u<<r);}
 bool ModalAllowed(U h){Event(2,h);return !(s.denied&(1u<<h));}U ModalWindow(){return s.modal;}
 U CurrentTask(){return s.task;}int SizeX(){return 2;}int SizeY(){return 3;}
 U HitMessage(U h,Point p){Event(3,h,U(p.x),U(p.y));if(s.mutation==h)s.valid&=~(1u<<h);return U(s.hits[h]);}
 U ComboChild(U h){return s.w[h].combo;}
};
extern "C" U window_hit_run(State*s,U op,U h,int x,int y,U flags){Provider e{*s};Point p{x,y};if(op>=4)return ControlHit(op==4?3:2,s->w[h].style,0x55);if(!op)return FromPoint(e,p);if(op==1)return Child(e,h,p,flags);if(op==2)return RealChild(e,h,p);return SizeParent(e,h);}
