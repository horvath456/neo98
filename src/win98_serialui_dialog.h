// SPDX-License-Identifier: GPL-2.0-or-later
// SERIALUI.DLL NE, source hash in win98_serialui.h.
#pragma once
namespace Win98SerialUi {
struct Choice { unsigned value, string; };
inline constexpr Choice Baud[]={{110,200},{300,216},{1200,201},{2400,202},{4800,203},{9600,204},{19200,206},{38400,207},{57600,211},{115200,210},{230400,220},{460800,221},{921600,222}};
inline constexpr Choice Data[]={{4,240},{5,241},{6,242},{7,243},{8,244}};
inline constexpr Choice Parities[]={{1,250},{2,251},{3,252},{4,253},{5,254}};
inline constexpr Choice Stops[]={{0,255},{1,256},{2,257}};
inline constexpr Choice Flows[]={{1,260},{2,261},{3,262}};
inline unsigned Dword(const unsigned char*p){return unsigned(p[0])|(unsigned(p[1])<<8)|(unsigned(p[2])<<16)|(unsigned(p[3])<<24);}
inline void Dword(unsigned char*p,unsigned v){for(unsigned i=0;i<4;++i)p[i]=v>>(i*8);}
// Item-data values, not list indices. Unknown settings select original defaults.
inline void DialogSelection(const unsigned char*d,unsigned*v){
 v[0]=9600;for(auto c:Baud)if(c.value==Dword(d+4))v[0]=c.value;
 v[1]=d[18]>=4&&d[18]<=8?d[18]:8;
 unsigned flags=Dword(d+8);v[2]=3;
 if(flags&2){const unsigned parity[]={3,2,1,4,5};if(d[19]>0&&d[19]<5)v[2]=parity[d[19]];}
 v[3]=d[20]<3?d[20]:0;
 v[4]=(flags&0x104)==0x100?1:(flags&0x104)==4?2:(flags&0x104)==0?3:1;
}
inline void DialogApply(unsigned char*d,const unsigned*v){
 Dword(d+4,v[0]);d[18]=v[1];unsigned flags=Dword(d+8);
 switch(v[2]&255){case 1:flags|=2;d[19]=2;break;case 2:flags|=2;d[19]=1;break;
 case 3:flags&=~2u;d[19]=0;break;case 4:flags|=2;d[19]=3;break;case 5:flags|=2;d[19]=4;break;}
 d[20]=v[3];
 switch(v[4]&255){case 1:flags=(flags|0x300)&0xffffcffbu;break;case 2:flags=((flags&~0x300u)|4)&~0x1000u;flags|=0x2000;break;case 3:flags&=0xffffccfbu;break;}
 Dword(d+8,flags);
}
}
namespace Win98SerialUi {
// NE. Receive bits use the original modulus, not a bitmask.
inline void FifoRead(const unsigned char*s,bool&enabled,unsigned&rx,unsigned&tx){enabled=s[0]!=0;rx=(s[3]%193)>>6;tx=s[1]%17;}
inline void FifoWrite(unsigned char*s,bool enabled,unsigned rx,unsigned tx){s[0]=enabled?2:0;s[1]=tx*5+1;s[2]=0;s[3]=rx*64;}
}
