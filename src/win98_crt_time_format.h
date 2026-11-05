// SPDX-License-Identifier: GPL-2.0-or-later
// Windows 98 CRT date and time formatting.
#pragma once
#include "win98_crt_time.h"
namespace Win98CrtTimeFormat {
using U=unsigned;using Tm=Win98CrtTime::Tm;
// Byte-addressed providers keep original partial writes and even the original
// %# numeric one-byte-capacity swap within checked guest memory.
template<class E> struct Formatter {
 E&e;const Tm&t;U out,left,maximum;bool hash=false;
 void Put(U c){e.Write8(out++,c);--left;}
 void Number(int value,U digits){
  if(hash){U begin=out;while(left>1){Put(U(value%10+48));value/=10;if(value<=0)break;}
   U a=begin,b=out-1;do{U x=e.Read8(a),y=e.Read8(b);e.Write8(b,x);--b;e.Write8(a,y);++a;}while(a<b);
  }else{
   if(digits>=left){left=0;return;}for(U i=digits;i;){--i;e.Write8(out+i,U(value%10+48));value/=10;}out+=digits;left-=digits;
  }
 }
 void Text(U index){for(U i=0;left&&e.Good();++i){U c=e.Label(index,i);if(!c)break;Put(c);}}
 void Zone(){for(U i=0;left&&e.Good();++i){U c=e.Zone(t.isdst!=0,i);if(!c)break;Put(c);}}
 bool Match(U index,U pos,const char*text){for(U i=0;;++i){U a=e.Label(index,pos+i),b=U(static_cast<unsigned char>(text[i]));if(a>='A'&&a<='Z')a+=32;if(b>='A'&&b<='Z')b+=32;if(a!=b)return false;if(!a)return true;}}
 void Pattern(U index){
  U pos=0;
  while(left&&e.Good()&&e.Label(index,pos)){
   U c=e.Label(index,pos),count=1;while(e.Label(index,pos+count)==c)++count;U next=pos+count;char code=0;
   if(c=='M'){if(count==1||count==2)code='m';else if(count==3)code='b';else if(count==4)code='B';}
   else if(c=='d'){if(count==1||count==2)code='d';else if(count==3)code='a';else if(count==4)code='A';}
   else if(c=='H'&&(count==1||count==2))code='H';
   else if(c=='h'&&(count==1||count==2))code='I';
   else if(c=='m'&&(count==1||count==2))code='M';
   else if(c=='s'&&(count==1||count==2))code='S';
   else if(c=='y'){if(count==2)code='y';else if(count==4)code='Y';}
   else if(c=='A'||c=='a'){code='p';if(Match(index,pos,"am/pm"))next=pos+5;else if(Match(index,pos,"a/p"))next=pos+3;}
   else if(c=='t'){
    if(!e.LoadTm())return;U label=t.hour>11?39:38,at=0;for(U i=0;i<count&&left;++i){U ch=e.Label(label,at++);if(e.Lead(ch)&&left>1){Put(ch);ch=e.Label(label,at++);}Put(ch);}pos=next;continue;
   }else if(c=='\''){
    pos=next;if(!(count&1))continue;
    while(left&&e.Good()&&(c=e.Label(index,pos))&&c!='\''){if(e.Lead(c)&&left>1){Put(c);c=e.Label(index,++pos);}Put(c);++pos;}if(e.Label(index,pos)=='\'')++pos;continue;
   }
   if(code){Dispatch(code);pos=next;continue;}
   // Unrecognized runs are copied one byte per pass, preserving DBCS pairs.
   if(e.Lead(c)){Put(c);++pos;c=e.Label(index,pos);}Put(c);++pos;
  }
 }
 void Dispatch(U code){
  constexpr char fields[]="aAbBpZzdHIMSmjwyYUW";
  for(char field:fields)if(field&&code==U(field)){if(!e.LoadTm())return;break;}
  switch(code){
  case '%':Put('%');break;
  case 'a':if(t.wday<0||t.wday>6){e.BadIndex();break;}Text(U(t.wday));break;
  case 'A':if(t.wday<0||t.wday>6){e.BadIndex();break;}Text(7+U(t.wday));break;
  case 'b':if(t.mon<0||t.mon>11){e.BadIndex();break;}Text(14+U(t.mon));break;
  case 'B':if(t.mon<0||t.mon>11){e.BadIndex();break;}Text(26+U(t.mon));break;
  case 'p':Text(t.hour>11?39:38);break;
  case 'Z':case 'z':Zone();break;
  case 'd':Number(t.mday,2);break;case 'H':Number(t.hour,2);break;case 'I':Number(t.hour%12?t.hour%12:12,2);break;
  case 'M':Number(t.min,2);break;case 'S':Number(t.sec,2);break;case 'm':Number(Win98CrtTime::Add(t.mon,1),2);break;
  case 'j':Number(Win98CrtTime::Add(t.yday,1),3);break;case 'w':Number(t.wday,1);break;
  case 'y':Number(t.year%100,2);break;case 'Y':Number(Win98CrtTime::Add(t.year,1900),4);break;
  case 'U':case 'W':{int weekday=code=='U'?t.wday:t.wday?t.wday-1:6;int week=t.yday<weekday?0:t.yday/7+(t.yday%7>=weekday);Number(week,2);break;}
  case 'X':hash=false;Pattern(42);break;
  case 'x':{bool longer=hash;if(longer)hash=false;Pattern(longer?41:40);break;}
  case 'c':{bool longer=hash;if(longer)hash=false;Pattern(longer?41:40);if(left){Put(' ');Pattern(42);}break;}
  default:break; // Original consumes an unknown conversion without output.
  }
 }
 U Run(U format){
  while(left&&e.Good()){
   U c=e.Read8(format);if(!c)break;
   if(c=='%'){++format;hash=false;if(e.Read8(format)=='#'){hash=true;++format;}Dispatch(e.Read8(format++));}
   else{if(e.Lead(c)&&left>1){Put(c);c=e.Read8(++format);}Put(c);++format;}
  }
  if(!e.Good()||!left)return 0;e.Write8(out,0);return maximum-left;
 }
};
// Fixed separators are independent of LC_TIME.
template<class C> void Short(const Win98Time::SystemTime&t,bool date,C*out){
 unsigned a=date?t.month:t.hour,b=date?t.day:t.minute,c=date?t.year%100:t.second;
 out[8]=0;out[2]=out[5]=date?'/':':';out[0]=C(a/10+48);out[1]=C(a%10+48);out[3]=C(b/10+48);out[4]=C(b%10+48);out[6]=C(c/10+48);out[7]=C(c%10+48);
}
struct Export{const char*name;U argc;};constexpr Export Exports[]={{"strftime",4},{"_Strftime",5},{"wcsftime",4},{"_strdate",1},{"_strtime",1},{"_wstrdate",1},{"_wstrtime",1}};
}
namespace Win98CrtTimeFormat {
// Wide formatting allocates narrow temporary buffers, runs
// strftime and then converts at most maximum WCHARs. An exact fit may omit NUL.
template<class E> U Wide(E&e,U output,U maximum,U format,U tm){
 U length=e.WideLength(format)+1,bytes=maximum*2,result=0;
 U text=e.Allocate(bytes);if(!text)return 0;
 U narrow=e.Allocate(length*2);
 if(narrow&&e.NarrowBuffer(narrow,format,length*2)!=~0u&&e.Format(text,bytes,narrow,tm)){
  U converted=e.WideBuffer(output,text,maximum);if(converted!=~0u)result=converted;
 }
 e.Free(text);e.Free(narrow);return result;
}
}
