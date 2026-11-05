// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_shell_registration.h"
using namespace Win98ShellRegistration;
struct Wire {State state;Window windows[2];U mutation=0;};
using Event=void(*)(U,U,U,U,U,U);
struct E:Environment {Wire&w;Event event;E(Wire&x,Event f):w(x),event(f){}Window ReadWindow(U h)override{return w.windows[h==0x18000?0:1];}void WriteFlags(U h,U f)override{w.windows[h==0x18000?0:1].flags=f;}void PositionBottom(U h)override{event(1,h,1,0,0,0x13);if(w.mutation)w.state.shell_record=w.mutation==1?0:0x19000;}void Send(W h,W m,W wp,U lp,W flags)override{event(2,h,m,wp,lp,flags);}};
extern "C" U shell_registration(Wire*w,Event f,U operation,U h){E e(*w,f);if(operation){Destroy(w->state,e.ReadWindow(h),h);return 0;}return Set(w->state,e,h);}

extern "C" void shell_position(unsigned window_flags,unsigned*after,unsigned*flags){ConstrainPosition(window_flags,*after,*flags);}
