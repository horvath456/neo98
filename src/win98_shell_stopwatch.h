// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98ShellStopwatch {
using U=unsigned;using Q=unsigned long long;
struct State { U storage=0,mode=0;Q frequency=0; };
// Stopwatch lifetime and event recording. The
// configured logger initialization is an explicit provider.
template<class E> void Initialize(E&e,State&s){U key=0,mode=0;if(e.Open(key))return;
 if(!e.Mode(key,mode)){if(!(mode&0x3449))mode|=1;s.storage=e.Allocate(0x94,0x40);if(!s.storage)mode=0;if(mode){s.mode=mode;e.Activate(s);}}
 e.Close(key);
}
// Ordinal 241 takes no arguments.
inline U Mode(const State&s){return s.storage?s.mode:0;}
// Low-64-bit multiplication and signed division with magnitude/sign
// operations. Using unsigned intermediates preserves overflow without C++ UB.
inline Q SignedDivide(Q numerator,Q denominator){bool minus=(numerator>>63)!=(denominator>>63);Q n=(numerator>>63)?0-numerator:numerator,d=(denominator>>63)?0-denominator:denominator;Q result=n/d;return minus?0-result:result;}
// Ordinal 250 caches frequency per module and returns a 64-bit result.
template<class E> Q Clock(E&e,State&s){if(!s.frequency)e.Frequency(s.frequency);Q count=0;if(!e.Counter(count)||!s.frequency){e.ClockFailure();return 0;}return SignedDivide(count*1000,s.frequency);}
}
