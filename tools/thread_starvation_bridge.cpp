// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_thread_control.h"
namespace W = Win98ThreadControl;
struct Environment {
    unsigned *a; W::PriorityState states[4];
    void RequeuePriority(unsigned, unsigned) {}
    void PropagatePriority(unsigned, unsigned) {}
    void ReschedulePriority() {}
    unsigned FirstReady() { return 1; }
    unsigned NextReady(unsigned t) { return t < a[1] ? t + 1 : 0; }
    W::PriorityState &Priority(unsigned t) { return states[t-1]; }
    unsigned LastRun(unsigned t) { return a[6+(t-1)*8+6]; }
    unsigned Maximum(unsigned t) { return a[6+(t-1)*8+7]; }
};
extern "C" void evaluate(unsigned *a, unsigned *out) {
    Environment e{a,{}};
    for(unsigned t=0;t<4;++t) {
        const unsigned *p=a+6+t*8;
        e.states[t]={p[0],p[1],p[2],p[3],p[4],p[5]};
    }
    unsigned period=a[3];
    if(a[0]==0) period=W::StarvationScan(e,a[2],a[3],a[4],a[5]);
    if(a[0]==1) W::BoostPriority(e,1,e.states[0],a[2],a[3],a[13]);
    if(a[0]==2) W::DecayPriority(e,1,e.states[0]);
    out[0]=period;
    for(unsigned t=0;t<4;++t) {
        auto &s=e.states[t]; unsigned *p=out+1+t*6;
        p[0]=s.base;p[1]=s.effective;p[2]=s.inherited;p[3]=s.boost;p[4]=s.decay;p[5]=s.adjustment;
    }
}
