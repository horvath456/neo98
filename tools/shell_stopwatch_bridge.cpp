// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_shell_stopwatch.h"
using U=unsigned;using Q=unsigned long long;using F=Q(*)(U,Q);
struct E{F f;U Open(U&key){key=9;return f(1,0);}U Mode(U,U&mode){mode=U(f(2,0));return U(f(3,0));}void Close(U){f(4,0);}U Allocate(U n,U flags){return U(f(5,(Q(flags)<<32)|n));}void Activate(Win98ShellStopwatch::State&s){f(6,s.mode);}void Frequency(Q&v){v=f(7,0);}bool Counter(Q&v){v=f(8,0);return true;}void ClockFailure(){f(9,0);}};
extern "C" Q shell_stopwatch_run(F f,U op,Win98ShellStopwatch::State*s){E e{f};if(op==0){Win98ShellStopwatch::Initialize(e,*s);return 0;}if(op==241)return Win98ShellStopwatch::Mode(*s);return Win98ShellStopwatch::Clock(e,*s);}
