// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_timezone.h"
extern "C" unsigned daylight_flags(unsigned flag,unsigned set,unsigned value,unsigned*out){
 Win98Timezone::State state;state.flag=flag;state.minutes=456;state.bias=123;state.start=789;state.end=1011;
 unsigned result=set?Win98Timezone::SetDaylight(state,value):Win98Timezone::GetDaylight(state);
 out[0]=state.flag;out[1]=state.minutes;out[2]=state.bias;out[3]=state.start;out[4]=state.end;return result;
}
