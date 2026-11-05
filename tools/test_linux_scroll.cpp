// SPDX-License-Identifier: GPL-2.0-or-later
#include "host_scroll_linux.h"
#include <cassert>
#include <limits>
int main()
{
    LinuxScroll::Wheel wheel;
    auto first=wheel.Convert(-.25,.125,false,1,1,1000);
    assert(first.kind==HostWheelStep::Scroll && first.out.y==-30 && first.out.x==15);
    assert((first.out.flags&(HostScrollFine|HostScrollBegin))==(HostScrollFine|HostScrollBegin));
    auto next=wheel.Convert(-1,0,false,1,1,1050);
    assert(next.out.y==-120 && (next.out.flags&HostScrollFine) && !(next.out.flags&HostScrollBegin));
    auto whole=wheel.Convert(1,1,false,1,1,2000);
    assert(whole.out.y==120 && whole.out.x==120 && whole.out.flags==0);
    auto scaled=wheel.Convert(.5,0,false,1,2,3000); assert(scaled.out.y==30);
    auto changed=wheel.Convert(.5,0,false,2,1,3050); assert(changed.out.flags&HostScrollBegin);
    for(int i=0;i<3;++i) assert(wheel.Convert(.25,0,true,3,1,4000+i).kind==HostWheelStep::Nothing);
    auto notch=wheel.Convert(.25,0,true,3,1,4003);assert(notch.steps==1);
    assert(wheel.Convert(std::numeric_limits<double>::quiet_NaN(),0,false,1,1,5000).kind==HostWheelStep::Nothing);
    puts("LINUX SCROLL PASS: fractions, horizontal axis, gesture boundaries, scale, focus, captured games");
}
