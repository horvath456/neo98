// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_file_times.h"
extern "C" unsigned file_times_run(unsigned set,const unsigned*a,const unsigned*config,unsigned*memory,unsigned*out){
 struct E {
  const unsigned*c;unsigned*m,*o;
  void Event(unsigned op,unsigned x=0,unsigned y=0,unsigned z=0){unsigned at=2+o[1]*4;o[at]=op;o[at+1]=x;o[at+2]=y;o[at+3]=z;++o[1];}
  void Lock(){Event(1);}void Unlock(){Event(9);}unsigned EnterFile(unsigned h){Event(2,h);return 0x77;}void LeaveFile(unsigned h){Event(8,h);}void Release(unsigned h){Event(7,h);}
  void Error(unsigned e){o[0]=e;Event(6,e);}
  unsigned Resolve(unsigned h,unsigned access){Event(3,h,access);if(c[0]==1){Error(6);return 0;}return 0x200800;}
  unsigned DosHandle(unsigned){return c[0]==2?~0u:0x55;}
  unsigned Get(unsigned h,unsigned i,Win98FileTimes::Dos&s){Event(4,i);s={0x1234+i,0x2345+i,i?0u:7u};return c[1]==i+1?c[2]:0;}
  unsigned Set(unsigned h,unsigned i,const Win98FileTimes::Dos&s){Event(5,i,s.time,s.hundredths);return c[1]==i+1?c[2]:0;}
  Win98FileTimes::Q ReadFileTime(unsigned p){return Win98FileTimes::Q(m[(p-0x200000)/4])|(Win98FileTimes::Q(m[(p-0x200000)/4+1])<<32);}
  void WriteFileTime(unsigned p,Win98FileTimes::Q q){m[(p-0x200000)/4]=unsigned(q);m[(p-0x200000)/4+1]=unsigned(q>>32);}
  bool FromDos(Win98FileTimes::Dos s,Win98FileTimes::Q&q){if(c[3])return false;q=Win98FileTimes::Q(s.date)|(Win98FileTimes::Q(s.time)<<16)|(Win98FileTimes::Q(s.hundredths)<<32);return true;}
  bool ToDos(Win98FileTimes::Q q,Win98FileTimes::Dos&s){if(c[3])return false;s={unsigned(q)&65535,unsigned(q>>16)&65535,unsigned(q>>32)&255};return true;}
 }e{config,memory,out};out[0]=777;out[1]=0;return Win98FileTimes::Run(e,a,set);
}
