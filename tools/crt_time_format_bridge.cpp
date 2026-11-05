// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_crt_time_format.h"
#include <string.h>
extern "C" unsigned crt_time_format(const char*fmt,const int*tm,unsigned capacity,const char*labels,unsigned lead,unsigned char*out){
 struct E{unsigned char memory[16384];const char*labels;bool okay=true;unsigned lead;
 bool Good(){return okay;}bool LoadTm(){return true;}unsigned Read8(unsigned p){if(p>=sizeof memory){okay=false;return 0;}return memory[p];}void Write8(unsigned p,unsigned v){if(p>=sizeof memory){okay=false;return;}memory[p]=v;}
 unsigned Label(unsigned i,unsigned at){if(i>=43||at>=128){okay=false;return 0;}return static_cast<unsigned char>(labels[i*128+at]);}
 bool Lead(unsigned c){return c==lead&&lead;}void BadIndex(){okay=false;}unsigned Zone(bool dst,unsigned i){return i<4?unsigned((dst?"PDT":"PST")[i]):0;}
 }e{{},labels,true,lead};memset(e.memory,0xa5,sizeof e.memory);memset(e.memory+8192,0,1024);strcpy((char*)e.memory+8192,fmt);Win98CrtTime::Tm t;memcpy(&t,tm,36);Win98CrtTimeFormat::Formatter<E>f{e,t,4096,capacity,capacity};auto result=f.Run(8192);memcpy(out,e.memory+4080,560);return result;
}
extern "C" void crt_short_time(const unsigned short*clock,unsigned date,unsigned wide,unsigned char*out){Win98Time::SystemTime t;memcpy(&t,clock,16);if(wide)Win98CrtTimeFormat::Short(t,date,(unsigned short*)out);else Win98CrtTimeFormat::Short(t,date,(char*)out);}
extern "C" unsigned crt_wide_time_format(const unsigned short*fmt,const int*tm,unsigned capacity,const char*labels,unsigned char*out){
 struct E{unsigned char memory[65536];const char*labels;Win98CrtTime::Tm t;unsigned next=0x9000;bool okay=true;
 bool Good(){return okay;}bool LoadTm(){return true;}unsigned Read8(unsigned p){if(p>=sizeof memory){okay=false;return 0;}return memory[p];}void Write8(unsigned p,unsigned v){if(p>=sizeof memory){okay=false;return;}memory[p]=v;}
 unsigned Label(unsigned i,unsigned at){if(i>=43||at>=128){okay=false;return 0;}return static_cast<unsigned char>(labels[i*128+at]);}bool Lead(unsigned){return false;}void BadIndex(){okay=false;}unsigned Zone(bool dst,unsigned i){return i<4?unsigned((dst?"PDT":"PST")[i]):0;}
 unsigned Read16(unsigned p){return Read8(p)|(Read8(p+1)<<8);}void Write16(unsigned p,unsigned c){Write8(p,c);Write8(p+1,c>>8);}
 unsigned WideLength(unsigned p){unsigned n=0;while(Read16(p+n*2))++n;return n;}unsigned Allocate(unsigned n){unsigned p=next;next+=(n?n:1)+16;return p;}void Free(unsigned){}
 unsigned NarrowBuffer(unsigned out,unsigned in,unsigned cap){unsigned n=0;for(;n<cap;++n){unsigned c=Read16(in+n*2);if(c>255)return ~0u;Write8(out+n,c);if(!c)return n;}return n;}
 unsigned WideBuffer(unsigned out,unsigned in,unsigned cap){unsigned n=0;for(;n<cap;++n){unsigned c=Read8(in+n);Write16(out+n*2,c);if(!c)return n;}return n;}
 unsigned Format(unsigned out,unsigned cap,unsigned fmt,unsigned){Win98CrtTimeFormat::Formatter<E>f{*this,t,out,cap,cap};return f.Run(fmt);}
 }e{{},labels,{}};memcpy(&e.t,tm,36);memset(e.memory+0xff0,0xa5,560);unsigned n=0;do{e.Write16(0x2000+n*2,fmt[n]);}while(fmt[n++]);auto r=Win98CrtTimeFormat::Wide(e,0x1000,capacity,0x2000,0x3000);memcpy(out,e.memory+0xff0,560);return r;
}
