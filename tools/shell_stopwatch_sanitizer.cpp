// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_shell_stopwatch.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using U=unsigned;using Q=unsigned long long;
struct E{U open=0,query=0,mode=0,activated=0,closed=0,frequency_calls=0;bool fail=false;Q frequency=1000000,counter=1234567;U Open(U&key){key=9;return open;}U Mode(U,U&v){v=mode;return query;}void Close(U){++closed;}U Allocate(U n,U flags){assert(n==148&&flags==64);return fail?0:0x10000000;}void Activate(Win98ShellStopwatch::State&s){activated=s.mode;}void Frequency(Q&v){++frequency_calls;v=frequency;}bool Counter(Q&v){v=counter;return true;}void ClockFailure(){assert(false);}};
int main(){for(U mode:{0u,1u,2u,4u,8u,0x3449u,0x10000u,0xffffffffu}){E e;e.mode=mode;Win98ShellStopwatch::State s;Win98ShellStopwatch::Initialize(e,s);assert(Win98ShellStopwatch::Mode(s)==(mode&0x3449?mode:mode|1));assert(e.closed==1&&e.activated==s.mode);}E e;Win98ShellStopwatch::State s;assert(Win98ShellStopwatch::Clock(e,s)==1234);e.counter=2345678;assert(Win98ShellStopwatch::Clock(e,s)==2345&&e.frequency_calls==1);assert(Win98ShellStopwatch::SignedDivide(0x8000000000000000ull,~0ull)==0x8000000000000000ull);assert(Win98ShellStopwatch::SignedDivide(~0ull,1000)==0);e.open=2;s={};Win98ShellStopwatch::Initialize(e,s);assert(!Win98ShellStopwatch::Mode(s));puts("PASS StopWatch original decision/arithmetic sanitizer");}
