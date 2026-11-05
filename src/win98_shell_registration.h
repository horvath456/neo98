// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98ShellRegistration {
using U=unsigned;using W=unsigned short;
static_assert(sizeof(U)==4 && sizeof(W)==2,"Win98 DWORD/WORD widths");
struct State {U shell_record=0;W task=0;};
struct Window {U style=0,owner=0,extended=0,flags=0;W task=0,handle=0;};
struct Environment {
 virtual ~Environment()=default;
 virtual Window ReadWindow(U)=0;
 virtual void WriteFlags(U,U)=0;
 virtual void PositionBottom(U)=0; // USER, insertAfter=1, flags=0013, x/y/cx/cy=0.
 virtual void Send(W handle,W message,W wp,U lp,W flags)=0; // USER.
};
inline U Set(State&s,Environment&e,U record){ // USER
 if(record){
  if(s.shell_record)return 0;
  const Window w=e.ReadWindow(record);
  if((w.style&0xc0000000u)==0x40000000u||w.owner||(w.extended&8))return 0;
 }else if(s.shell_record){const U old=s.shell_record;const Window w=e.ReadWindow(old);e.WriteFlags(old,w.flags&~0x10000000u);}
 s.shell_record=record;
 if(record){const Window w=e.ReadWindow(record);s.task=w.task;e.WriteFlags(record,w.flags|0x10000000u);e.PositionBottom(record);}
 if(s.shell_record){const Window w=e.ReadWindow(s.shell_record);e.Send(w.handle,0x34,4,1,0);}
 return 1;
}
inline void ConstrainPosition(U window_flags,U&after,U&flags){ // USER
 if(window_flags&0x10000000u){flags&=~4u;after=1;}
}
inline void Destroy(State&s,const Window&w,U record){ // USER
 if(!(w.style&0x40000000u)&&!w.owner&&s.shell_record==record)s.shell_record=0;
}
} // namespace Win98ShellRegistration
