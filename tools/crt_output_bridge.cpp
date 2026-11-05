// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_crt_output.h"
struct E{
 unsigned char*memory;bool good=true;unsigned error=0;
 unsigned Read8(unsigned p){if(p<0x30000000||p>=0x30010000){good=false;return 0;}return memory[p-0x30000000];}
 unsigned Read16(unsigned p){return Read8(p)|(Read8(p+1)<<8);}unsigned Read32(unsigned p){return Read16(p)|(Read16(p+2)<<16);}
 void Write8(unsigned p,unsigned v){if(p<0x30000000||p>=0x30010000){good=false;return;}memory[p-0x30000000]=v;}
 void Write16(unsigned p,unsigned v){Write8(p,v);Write8(p+1,v>>8);}void Write32(unsigned p,unsigned v){Write16(p,v);Write16(p+2,v>>16);}
 bool Good(){return good;}void Unsupported(const char*){good=false;}
 bool Widen(unsigned c,unsigned&v){v=c&255;return true;}
 bool Narrow(unsigned c,unsigned&v){if(c>255){Write32(0x30006000,42);return false;}v=c;return true;}
 void Floating(unsigned long long,unsigned,unsigned,unsigned){good=false;}
};
extern "C" unsigned crt_output_run(unsigned char*memory,unsigned index,unsigned capacity){auto x=Win98CrtOutput::Exports[index];E e{memory};return Win98CrtOutput::Format(e,0x30003000,capacity,0x30001000,0x30002000,x.wide,x.bounded);}
