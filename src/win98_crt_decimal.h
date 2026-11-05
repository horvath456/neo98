// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_crt_decimal_tables.h"
namespace Win98CrtDecimal {
using U=unsigned;using Q=unsigned long long;using Big=__uint128_t;
struct Extended {unsigned short w[6]{};};
inline Big Mantissa(const Extended&a){Big v=0;for(int i=4;i>=0;--i)v=(v<<16)|a.w[i];return v;}
inline Extended Make(Big value,U exponent){Extended r;for(U i=0;i<5;++i){r.w[i]=unsigned(value)&65535;value>>=16;}r.w[5]=exponent;return r;}
inline constexpr Big Mask80=(Big(1)<<80)-1,Mask96=(Big(1)<<96)-1;
// Decimal bytes contain individual digits.
inline Extended Digits(const unsigned char*d,U count){Big value=0;for(U i=0;i<count;++i)value=value*10+d[i];U exponent=0x404e;if(!value)return {};while(!(value&(Big(1)<<79))){value<<=1;--exponent;}return Make(value,exponent);}
// Windows 98 omits product diagonals below 4.
inline void Multiply(Extended&a,const Extended&b){
 U x=a.w[5]&32767,y=b.w[5]&32767,sign=(a.w[5]^b.w[5])&32768,sum=x+y;
 if(x==32767||y==32767||sum>0xbffd){a=Make(Big(1)<<79,sign|32767);return;}
 if(sum<=0x3fbf){a={};return;}
 if(!x){++sum;if(!Mantissa(a)){a.w[5]=sign;return;}}
 if(!y){++sum;if(!Mantissa(b)){a={};return;}}
 unsigned short words[6]{};
 for(U i=0;i<5;++i)for(U j=0;j<5-i;++j){
  U before=U(words[i])|(U(words[i+1])<<16),product=U(a.w[i+j])*U(b.w[4-j]),next=before+product;
  words[i]=next&65535;words[i+1]=next>>16;if(next<before&&i+2<6)++words[i+2];
 }
 Big value=0;for(int i=5;i>=0;--i)value=(value<<16)|words[i];
 int exponent=int((sum+0xc002)&65535);if(exponent&32768)exponent-=65536;
 while(exponent>0&&!(value&(Big(1)<<95))){value=(value<<1)&Mask96;--exponent;}
 if(exponent<=0){--exponent;if(exponent<0){U shift=U(-exponent);bool sticky=shift>=96?value!=0:(value&((Big(1)<<shift)-1))!=0;value=shift>=96?0:value>>shift;if(sticky)value|=1;exponent=0;}}
 U guard=U(value)&65535;bool round=guard>0x8000||(U(value)&0x1ffff)==0x18000;
 value>>=16;if(round){++value;if(value>Mask80){value=Big(1)<<79;++exponent;}}
 a=exponent>=32767?Make(Big(1)<<79,sign|32767):Make(value,sign|U(exponent));
}
// Use base-8 exponent groups and the local tables.
inline void Scale(Extended&a,int exponent,bool all80=false){
 if(!exponent)return;if(!all80)a.w[0]=0;
 const auto&powers=exponent<0?NegativePowers:PositivePowers;U amount=exponent<0?U(-exponent):U(exponent),group=0;
 while(amount){U digit=amount&7;amount>>=3;if(digit){Extended b;for(U i=0;i<6;++i)b.w[i]=powers[group*7+digit-1][i];
   if(b.w[0]>=0x8000){U pair=U(b.w[1])|(U(b.w[2])<<16);--pair;b.w[1]=pair&65535;b.w[2]=pair>>16;}
   Multiply(a,b);
  }++group;}
}
struct Scan {Extended number;U end=0,flags=0;};
// The public adapters pass zero auxiliary arguments.
// E supplies byte reads, locale digit classification and the decimal-point byte.
template<class E> Scan Parse(E&e,U start){
 Scan out;U p=start,saved=start,state=0,count=0,sign=0;int adjustment=0,exponent=0,exponent_sign=1;bool any=false;unsigned char digits[25]{};
 auto digit=[&](U c){return e.Digit(c);};auto nonzero=[](U c){return c>='1'&&c<='9';};
 while(e.Byte(p)==' '||e.Byte(p)=='\t'||e.Byte(p)=='\n'||e.Byte(p)=='\r')++p;
 bool done=false;
 while(!done){U c=e.Byte(p++);
  switch(state){
   case 0:if(nonzero(c)){state=3;--p;}else if(c==e.Decimal())state=5;else if(c=='+'||c=='-'){sign=c=='-'?32768:0;state=2;}else if(c=='0')state=1;else{--p;done=true;}break;
   case 1:any=true;if(nonzero(c)){state=3;--p;}else if(c==e.Decimal())state=4;else if(c=='0')state=1;else if(c=='D'||c=='E'||c=='d'||c=='e')state=6;else{--p;done=true;}break;
   case 2:if(nonzero(c)){state=3;--p;}else if(c==e.Decimal())state=5;else if(c=='0')state=1;else{p=saved;done=true;}break;
   case 3:any=true;
    while(digit(c)){if(count<25)digits[count++]=c-'0';else ++adjustment;c=e.Byte(p++);}
    if(c==e.Decimal())state=4;else if(c=='D'||c=='E'||c=='d'||c=='e')state=6;else{--p;done=true;}break;
   case 4:any=true;
    if(!count)while(c=='0'){--adjustment;c=e.Byte(p++);}
    while(digit(c)){if(count<25){digits[count++]=c-'0';--adjustment;}c=e.Byte(p++);}
    if(c=='D'||c=='E'||c=='d'||c=='e')state=6;else{--p;done=true;}break;
   case 5:if(digit(c)){state=4;--p;}else{p=saved;done=true;}break;
   case 6:saved=p-2;if(nonzero(c)){state=9;--p;}else if(c=='+'||c=='-'){exponent_sign=c=='-'?-1:1;state=7;}else if(c=='0')state=8;else{p=saved;done=true;}break;
   case 7:if(nonzero(c)){state=9;--p;}else if(c=='0')state=8;else{p=saved;done=true;}break;
   case 8:while(c=='0')c=e.Byte(p++);if(nonzero(c)){state=9;--p;}else{--p;done=true;}break;
   case 9:while(digit(c)){exponent=exponent*10+int(c)-'0';if(exponent>5200){exponent=5201;break;}c=e.Byte(p++);}while(digit(c))c=e.Byte(p++);--p;done=true;break;
   default:done=true;break;
  }
 }
 out.end=p;
 if(!any){out.flags=4;out.number=Make(0,sign);return out;}
 if(count>24){if(digits[23]>=5)++digits[23];count=24;++adjustment;}
 while(count&&digits[count-1]==0){--count;++adjustment;}
 if(!count){out.number=Make(0,sign);return out;}
 exponent=exponent*exponent_sign+adjustment;
 if(exponent>5200){out.flags=2;out.number=Make(Big(1)<<79,sign|32767);return out;}
 if(exponent< -5200){out.flags=1;out.number=Make(0,sign);return out;}
 out.number=Digits(digits,count);Scale(out.number,exponent);out.number.w[5]|=sign;return out;
}
// Preserve the Windows 98 tests for discarded bits.
inline bool Round96(Big&value,U precision){U shift=95-precision;bool overflow=false;
 if(value&(Big(1)<<shift)){
  const U tail=94-precision;if(value&((Big(1)<<tail)-1)){value+=Big(1)<<(shift+1);overflow=value>Mask96;value&=Mask96;}
 }
 value&=Mask96^((Big(1)<<shift)-1);return overflow;
}
struct Packed{Q bits=0;U status=0;};
// Pack an extended value into an IEEE double.
inline Packed Double(const Extended&a){
 Packed out;Q sign=Q(a.w[5]&32768)<<48;int exponent=int(a.w[5]&32767)-16383;Big value=Mantissa(a)<<16,original=value;
 if(exponent==-16383){out.bits=sign;out.status=value?2:0;return out;}
 if(Round96(value,53))++exponent;
 if(exponent< -1076){out.bits=sign;out.status=2;return out;}
 if(exponent<=-1023){U shift=U(-1023-exponent);value=original>>shift;Round96(value,53);value>>=12;out.bits=sign|Q(value>>32);out.status=2;return out;}
 if(exponent>=1024){out.bits=sign|0x7ff0000000000000ull;out.status=1;return out;}
 value&=~(Big(1)<<95);value>>=11;out.bits=sign|(Q(exponent+1023)<<52)|Q(value>>32);return out;
}
struct Result {Q bits=0;U end=0;bool range=false;};
// Parse a decimal string and report the end position and range status.
template<class E>Result Convert(E&e,U start,bool atof){U first=start;while(e.Space(e.Byte(first)))++first;
 Scan scan=Parse(e,first);Packed packed;if(!(scan.flags&4))packed=Double(scan.number);
 U status=scan.flags&4?0x200:0;if((scan.flags&2)||packed.status==1)status=0x80;if((scan.flags&1)||packed.status==2)status|=0x100;
 Result out{packed.bits,scan.end,false};if(atof)return out;
 if(status&0x240){out.bits=0;out.end=start;}
 else if(status&0x81){out.bits=0x7ff0000000000000ull|(e.Byte(first)=='-'?0x8000000000000000ull:0);out.range=true;}
 else if(status&0x100){out.bits=0;out.range=true;}
 return out;
}
}
