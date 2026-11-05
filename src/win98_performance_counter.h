// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98Performance {
using U=unsigned;using Q=unsigned long long;
constexpr U Frequency=0x1234dc; // Original KERNEL32, PIT ticks/second.
// Latch PIT0 while interrupts are disabled, then restore the caller's flags.
struct PitState {U low,high,reload,last;};
template<class E> Q ReadPit(E&e,PitState&s){
 U sample=e.LatchPit();if(sample>s.last)sample=0;s.last=sample;
 U delta=s.reload-sample,low=s.low+delta,high=s.high+(low<s.low);
 return (Q(high)<<32)|low;
}
// Store the counter low word before the high word, then return one.
template<class E> U Pack(E&e,U output,Q value){e.Write32(output,U(value));e.Write32(output+4,U(value>>32));return 1;}
// KERNEL32, and 's VWIN32 service2A0044 call.
template<class E> U Query(E&e,U output,bool frequency){
 if(!e.Probe(output)){e.Error(87);return 0;}
 if(frequency)return Pack(e,output,Frequency);
 return e.VwinQuery(output);
}
// ARM provider: one real monotonic microsecond clock, exposed in original PIT
// units. Splitting before multiplying avoids overflow in ordinary long uptimes.
inline Q FromMicroseconds(Q us){return us/1000000*Frequency+(us%1000000)*Frequency/1000000;}
}
