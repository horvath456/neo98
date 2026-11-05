/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_updown.h"
#include <cstring>
static unsigned char memory[65536];
struct Env {
 unsigned Read32(unsigned a){unsigned v;std::memcpy(&v,memory+a-0x100000,4);return v;}
 void Write32(unsigned a,unsigned v){std::memcpy(memory+a-0x100000,&v,4);}
 bool ValidBuddy(unsigned t){return Read32(t+0x24)!=0;}unsigned ListPosition(unsigned){return Read32(0x10e008);}
 void BuddyText(unsigned,unsigned short*out,unsigned n){std::memcpy(out,memory+0xd000,n*2);out[n-1]=0;}
 unsigned short Thousands(){return ',';}void WriteBuddy(unsigned){Write32(0x10e000,Read32(0x10e000)+1);}
 unsigned Reallocate(unsigned t,unsigned){return t;}void Relocated(unsigned){}
 bool Enabled(unsigned){return true;}unsigned Now(){return Read32(0x10e004);}void Dirty(unsigned){}
 bool Alive(unsigned){return true;}void DivideFault(){Write32(0x10e01c,1);}
 unsigned Notify(unsigned,unsigned p,unsigned&delta){Write32(0x10e00c,p);Write32(0x10e010,delta);if(Read32(0x10e018))delta=Read32(0x10e018);return Read32(0x10e014);}
 void Scroll(unsigned,unsigned){Write32(0x10e020,Read32(0x10e020)+1);}
};
extern "C" void*updown_memory(){return memory;}
extern "C" unsigned updown_run(unsigned op,unsigned t,unsigned a,unsigned b,unsigned c){Env e;switch(op){case 0:return Win98UpDown::Clamp(e,t);case 1:return Win98UpDown::Compare(e,t,a,b,c);case 2:return Win98UpDown::GetPosition(e,t,a);case 3:return Win98UpDown::SetPosition(e,t,a);case 4:return Win98UpDown::SetBase(e,t,a);case 5:return Win98UpDown::SetAccel(e,t,a,b);case 6:return Win98UpDown::GetAccel(e,t,a,b);case 7:Win98UpDown::Step(e,t);return 0;case 8:return Win98UpDown::Hit(a,b,c,short(t),short(t>>16));}return 0;}
