// SPDX-License-Identifier: GPL-2.0-or-later
// Serial driver and VCOMM request handling.
#pragma once
namespace Win98SerialDriver {
inline unsigned Read(const unsigned char*p){return unsigned(p[0])|(unsigned(p[1])<<8)|(unsigned(p[2])<<16)|(unsigned(p[3])<<24);}
inline unsigned Word(const unsigned char*p){return p[0]|(unsigned(p[1])<<8);}
inline void Write(unsigned char*p,unsigned v){for(unsigned i=0;i<4;++i)p[i]=v>>(8*i);}
inline void Word(unsigned char*p,unsigned v){p[0]=v;p[1]=v>>8;}
inline void FromDcb(const unsigned char*in,unsigned char*out){
 Write(out,48);Write(out+4,Read(in+4));unsigned f=Read(in+8),v=((f&14)<<1)|1|(f&0xf00);
 if(f&0x40)v|=0x10000;if(f&0x80)v|=0x20000;if(f&0x4000)v|=0x80000;
 const unsigned dtr[]={0x80,0x40000,0x2000,0},rts[]={2,0x100000,0x4000,0x200000};
 v|=dtr[(f>>4)&3]|rts[(f>>12)&3];Write(out+8,v);
 Write(out+12,Word(in+14));Write(out+16,Word(in+16));Word(out+20,Word(in+12));
 for(unsigned i=0;i<8;++i)out[22+i]=in[18+i];
 Write(out+32,0);Write(out+36,0);Write(out+40,0);
}
inline void ToDcb(const unsigned char*in,unsigned char*out){
 Write(out,28);Write(out+4,Read(in+4));unsigned f=Read(in+8),v=((f>>1)&14)|(f&0xf00)|(f&1);
 if(f&0x10000)v|=0x40;if(f&0x20000)v|=0x80;if(f&0x80000)v|=0x4000;
 if(!(f&0x80))v|=(f&0x2000)?0x20:0x10;
 if(!(f&2)){if(f&0x100000)v|=0x1000;else{v|=0x2000;if(!(f&0x4000))v|=0x1000;}}
 Write(out+8,v);Word(out+14,Read(in+12));Word(out+16,Read(in+16));Word(out+12,Word(in+20));
 for(unsigned i=0;i<8;++i)out[18+i]=in[22+i];
}
inline unsigned Divisor(unsigned baud){
 const unsigned divisors[]={1047,384,192,96,48,24,12,8,6,0,0,3,0,0,0,2};
 if(baud>=0xff10&&baud<=0xff1f)return divisors[baud-0xff10];
 return baud>=2?115200/baud:0;
}
// On success low byte is the UART line-control register, high byte the data
// mask. Error values are SERIAL's internal negative error codes.
inline bool Line(unsigned bytes,unsigned parity,unsigned stop,unsigned&value,unsigned&mask){
 if(parity>4){value=0xfffffffbu;return false;}
 mask=parity?0xff1e:0xfb1a;
 if(bytes<5||bytes>8){value=0xfffffff5u;return false;}
 unsigned line=(bytes-5)|((parity?parity*2-1:0)<<3)|4;
 if(stop>2||(stop==1&&(line&3))){value=0xfffffffbu;return false;}
 if(stop!=2)line&=~4u;
 value=line|(((0xffu<<bytes)>>8)<<8);return true;
}
inline bool Accept(const unsigned char*dcb){unsigned value=0,mask=0;return Divisor(Read(dcb+4))&&Line(dcb[18],dcb[19],dcb[20],value,mask);}
inline void Normalize(unsigned char*dcb){
 unsigned char internal[48]{};FromDcb(dcb,internal);unsigned value=0,mask=0;
 if(Line(dcb[18],dcb[19],dcb[20],value,mask))internal[8]&=mask>>8;
 ToDcb(internal,dcb);
}
// SERIAL SetState with the VCOMM Win32 mask 0xfff copies the public fields,
// including its reserved WORD. The caller's final WORD is untouched.
inline void SetDcb(unsigned char*state,const unsigned char*input){
 for(unsigned i=0;i<26;++i)state[i]=input[i];
 Normalize(state);
}
inline void Properties(unsigned char*out,unsigned rx,unsigned tx){
 const unsigned fields[]={0x00020040,1,0,0,0,0x10000000,1,255,127,0x1000ffff,0x1f07000f,tx,rx,0,0,0};
 for(unsigned i=0;i<16;++i)Write(out+4*i,fields[i]);
}
}
