// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98CrtOutput {
using U=unsigned;using Q=unsigned long long;
struct Export{const char*name;U ordinal,argc;bool wide,bounded,vector;};
inline constexpr Export Exports[]={
 {"sprintf",691,2,false,false,false},{"vsprintf",733,3,false,false,true},
 {"_snprintf",431,3,false,true,false},{"_vsnprintf",482,4,false,true,true},
 {"swprintf",716,2,true,false,false},{"vswprintf",734,3,true,false,true},
 {"_snwprintf",432,3,true,true,false},{"_vsnwprintf",483,4,true,true,true}
};
// Shared character classes and state transitions for formatted output.
inline constexpr unsigned char Table[]={
 6,0,0,6,0,1,0,0,0x10,0,3,6,0,6,2,0x10,4,0x45,0x45,0x45,5,5,5,5,5,0x35,0x30,0,0x50,0,0,0,
 0,0x20,0x28,0x38,0x50,0x58,7,8,0,0x37,0x30,0x30,0x57,0x50,7,0,0,0x20,0x20,8,0,0,0,0,
 8,0x60,0x68,0x60,0x60,0x60,0x60,0,0,0x70,0x70,0x78,0x78,0x78,0x78,8,7,8,0,0,7,0,8,8,8,0,0,8,0,8,0,7,8
};
// FILE-backed string output uses these decisions and eight
// wrappers. Capacity is in bytes internally; a full buffer gets no terminator.
template<class E>struct Sink{
 E&e;U pointer,remaining,wide,count=0;bool failed=false;
 void Byte(U value){remaining-=1;if(int(remaining)<0){failed=true;return;}e.Write8(pointer++,value);}
 void Put(U value){if(failed||!e.Good())return;Byte(value);if(wide&&!failed)Byte(value>>8);if(!failed)++count;}
 void Repeat(U value,int n){if(n>65536){e.Unsupported("MSVCRT output width exceeds native bound");return;}while(n-->0&&!failed&&e.Good())Put(value);}
 U Finish(){U result=failed?~0u:count;if(e.Good()){Byte(0);if(wide)Byte(0);}return result;}
};
// Narrow and wide formatted output share the format parser.
// Memory, SBCS conversion and floating-point formatting are providers.
template<class E>U Format(E&e,U output,U capacity,U format,U arguments,bool wide,bool bounded){
 Sink<E>sink{e,output,bounded?(wide?capacity*2:capacity):0x7fffffffu,U(wide)};
 U state=0,flags=0,width=0,precision=~0u;U scanned=0;
 auto character=[&](U p){return wide?e.Read16(p):e.Read8(p);};
 auto next=[&](){U value=e.Read32(arguments);arguments+=4;return value;};
 const U stride=wide?2:1;
 while(e.Good()&&!sink.failed){
  if(++scanned>8192){e.Unsupported("MSVCRT format exceeds native bound");break;}
  U ch=character(format);format+=stride;if(!ch)break;
  U kind=ch>=0x20&&ch<=0x78?(Table[ch-0x20]&15):0;
  state=Table[kind*8+state]>>4;
  if(state==0){sink.Put(ch);continue;}
  if(state==1){flags=width=0;precision=~0u;continue;}
  if(state==2){flags|=ch==' '?2:ch=='#'?0x80:ch=='+'?1:ch=='-'?4:ch=='0'?8:0;continue;}
  if(state==3){if(ch=='*'){width=next();if(int(width)<0){width=0-width;flags|=4;}}else width=width*10+ch-'0';continue;}
  if(state==4){precision=0;continue;}
  if(state==5){if(ch=='*'){precision=next();if(int(precision)<0)precision=~0u;}else precision=precision*10+ch-'0';continue;}
  if(state==6){
   if(ch=='h')flags|=0x20;else if(ch=='w')flags|=0x800;else if(ch=='l'&&!wide)flags|=0x10;
   else if(ch=='I'){if(character(format)=='6'&&character(format+stride)=='4'){flags|=0x8000;format+=stride*2;}else{state=0;sink.Put(ch);}}
   continue;
  }
  if(state!=7)continue;
  if(int(width)>65536||(int(precision)>8192)){e.Unsupported("MSVCRT field exceeds native bound");break;}
  if(ch=='n'){U pointer=next();if(flags&0x20)e.Write16(pointer,sink.count);else e.Write32(pointer,sink.count);continue;}
  if(ch=='e'||ch=='E'||ch=='f'||ch=='g'||ch=='G'){
   Q value=next();value|=Q(next())<<32;
   e.Floating(value,ch,precision,flags);break;
  }
  U length=0,text=0;bool text_wide=false,is_string=false,literal_null=false,converted_character=false;
  U single=0;char digits[65]={},prefix[2]={};U digit_count=0,zeroes=0,prefix_count=0;
  if(ch=='c'||ch=='C'){
   if(ch=='C'&&!(flags&0x830))flags|=wide?0x20:0x800;
   single=next();bool input_wide=wide?!(flags&0x20):(flags&0x810)!=0;
   if(input_wide)single&=65535;else single&=255;
   if(input_wide!=wide){U result=0;bool okay=wide?e.Widen(single,result):e.Narrow(single,result);if(!okay)continue;single=result;}
   converted_character=true;length=1;
  }else if(ch=='s'||ch=='S'||ch=='Z'){
   if(ch=='S'&&!(flags&0x830))flags|=wide?0x20:0x800;
   text_wide=ch=='Z'?(flags&0x800)!=0:wide?!(flags&0x20):(flags&0x810)!=0;
   text=next();is_string=true;
   if(ch=='Z'){
    if(text){U descriptor=text;text=e.Read32(descriptor+4);length=U(int(short(e.Read16(descriptor))));if(text_wide)length>>=1;}
    if(!text){literal_null=true;text_wide=false;length=6;}
    if(length>8192){e.Unsupported("MSVCRT counted string exceeds native bound");break;}
   }else{
    literal_null=!text;U limit=precision==~0u?8192:precision;
    while(length<limit){U value=literal_null?(length<6?U("(null)"[length]):0):text_wide?e.Read16(text+length*2):e.Read8(text+length);if(!value||!e.Good())break;++length;}
    if(length==8192&&precision==~0u){e.Unsupported("MSVCRT string exceeds native bound");break;}
   }
  }else{
   U radix=ch=='o'?8:(ch=='x'||ch=='X'||ch=='p')?16:10;bool signed_value=ch=='d'||ch=='i';
   if(ch=='p')precision=8;
   Q value=next();if(flags&0x8000)value|=Q(next())<<32;
   else if(flags&0x20)value=signed_value?Q((long long)short(value)):(value&65535);
   else if(signed_value)value=Q((long long)int(U(value)));
   bool negative=signed_value&&(value>>63);if(negative)value=Q(0)-value;
   if(signed_value){if(negative)prefix[prefix_count++]='-';else if(flags&1)prefix[prefix_count++]='+';else if(flags&2)prefix[prefix_count++]=' ';}
   if(radix==16&&(flags&0x80)&&value){prefix[0]='0';prefix[1]=(ch=='x'?'x':'X');prefix_count=2;}
   if(int(precision)<0)precision=1;else flags&=~8u;
   const char*alphabet=ch=='x'?"0123456789abcdef":"0123456789ABCDEF";
   while(value){digits[digit_count++]=alphabet[value%radix];value/=radix;}
   zeroes=precision>digit_count?precision-digit_count:0;
   if(radix==8&&(flags&0x80)&&!zeroes)zeroes=1;
   length=digit_count+zeroes;
  }
  int padding=int(width-prefix_count-length);
  if(!(flags&12))sink.Repeat(' ',padding);
  for(U i=0;i<prefix_count;++i)sink.Put(prefix[i]);
  if((flags&8)&&!(flags&4))sink.Repeat('0',padding);
  if(converted_character)sink.Put(single);
  else if(is_string){for(U i=0;i<length&&!sink.failed&&e.Good();++i){U value=literal_null?U("(null)"[i]):text_wide?e.Read16(text+i*2):e.Read8(text+i);
    if(text_wide!=wide){U result=0;if(!(wide?e.Widen(value,result):e.Narrow(value,result)))break;value=result;}sink.Put(value);}}
  else{sink.Repeat('0',int(zeroes));while(digit_count&&!sink.failed&&e.Good())sink.Put(digits[--digit_count]);}
  if(flags&4)sink.Repeat(' ',padding);
 }
 U result=sink.Finish();return e.Good()?result:~0u;
}
}
