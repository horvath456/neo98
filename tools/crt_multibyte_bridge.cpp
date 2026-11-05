// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_crt_multibyte.h"
#include "../src/win98_crt_search.h"
#include <cassert>
#include <cstring>
using namespace Win98CrtMultibyte;
struct Input{State state;U ansi,oem,locale,cp_success,cp_max;unsigned char cp_lead[12],memory[4096];U trace[32],count;};
struct Bridge{
 Input&s;void Event(U op,U arg){assert(s.count+2<=32);s.trace[s.count++]=op;s.trace[s.count++]=arg;}
 U Read8(U p){assert(p>=0x600000&&p<0x601000);return s.memory[p-0x600000];}U Read32(U p){U v;assert(p>=0x600000&&p<=0x600ffc);memcpy(&v,s.memory+p-0x600000,4);return v;}
 bool Multibyte(){return s.state.multibyte!=0;}bool LeadByte(U c){return Lead(s.state,c);}
 void Lock(U n){Event(0,n);}void Unlock(U n){Event(1,n);}
 U Ansi(){Event(2,0);return s.ansi;}U Oem(){Event(3,0);return s.oem;}U LocalePage(){return s.locale;}
 bool CpInfo(U p,U&m,unsigned char*l){Event(4,p);m=s.cp_max;memcpy(l,s.cp_lead,12);return s.cp_success;}
 void CaseTable(State&){Event(5,0);}
};
extern "C" U multibyte_run(Input*s,U op,U a,U b,U n){Bridge e{*s};switch(op){case 0:return SetPage(e,s->state,a);case 1:return GetPage(s->state);case 2:return Lead(s->state,a);case 3:return Trail(s->state,a);case 4:return Compare(e,a,b,0,0);case 5:return Compare(e,a,b,n,1);case 6:return Compare(e,a,b,n,2);case 7:return Length(e,a);case 8:return Increment(e,a);case 9:return Decrement(e,a,b);case 10:return Advance(e,a,n);case 11:return Character(e,a);case 12:return CharacterBytes(e,a);case 13:return ByteCount(e,a,n);case 14:return CharacterCount(e,a,n);case 15:return Boundary(e,a,b,false);case 16:return Boundary(e,a,b,true);case 17:return Win98CrtSearch::MultiFind(e,a,n);case 18:return Win98CrtSearch::MultiFindLast(e,a,n);}return 0;}
