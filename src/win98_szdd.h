// SPDX-License-Identifier: GPL-2.0-or-later
// SZDD method A: 4 KiB ring, LSB-first literal flags, 12-bit match offsets.
#pragma once
namespace Win98Szdd {
inline bool Header(const unsigned char*p,unsigned n){
 const unsigned char magic[]={83,90,68,68,136,240,39,51};
 if(n<14)return false;for(unsigned i=0;i<8;++i)if(p[i]!=magic[i])return false;return true;
}
inline unsigned Size(const unsigned char*p){return p[10]|unsigned(p[11])<<8|unsigned(p[12])<<16|unsigned(p[13])<<24;}
inline int DecodeAvailable(const unsigned char*input,unsigned size,unsigned char*out,unsigned length,unsigned &done){
 done=0;
 if(!Header(input,size))return -3;if(input[8]!='A')return -8;
 unsigned char ring[4096];for(auto &c:ring)c=32;
 unsigned pos=14,write=4080,flags=0,bits=0;
 while(done<length){
  if(!bits){if(pos==size)return -3;flags=input[pos++];bits=8;}
  if(flags&1){if(pos==size)return -3;unsigned char c=input[pos++];out[done++]=c;ring[write]=c;write=(write+1)&4095;}
  else{
   if(size-pos<2)return -3;unsigned from=input[pos++],packed=input[pos++];from|=(packed&240)<<4;
   unsigned count=(packed&15)+3;
   while(count--&&done<length){unsigned char c=ring[from];from=(from+1)&4095;out[done++]=c;ring[write]=c;write=(write+1)&4095;}
  }
  flags>>=1;--bits;
 }
 return 0;
}
inline int Decode(const unsigned char*input,unsigned size,unsigned char*out,unsigned length){
 unsigned done=0;return DecodeAvailable(input,size,out,length,done);
}
}
