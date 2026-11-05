// SPDX-License-Identifier: GPL-2.0-or-later
// Windows 98 CRT time conversion and daylight-saving rules.
#pragma once
#include "win98_timezone.h"
namespace Win98CrtTime {
using U=unsigned;using Q=unsigned long long;
inline int I(U n){return n<=0x7fffffff?int(n):-1-int(~n);}
inline int Add(int a,int b){return I(U(a)+U(b));}
inline int Mul(int a,int b){return I(U(a)*U(b));}
struct Tm{int sec,min,hour,mday,mon,year,wday,yday,isdst;};
static_assert(sizeof(Tm)==36,"Win98 CRT tm");
// Day before the start of each month, for common and leap years.
constexpr int Ends[2][13]={{-1,30,58,89,119,150,180,211,242,272,303,333,364},{-1,30,59,90,120,151,181,212,243,273,304,334,365}};
struct Transition{int year=-1,day=0,millis=0;};
struct State{
 int timezone=28800,daylight=1,dstbias=-3600;bool initialized=false,os_zone=false;
 Win98Timezone::Zone zone{};Transition start{},end{};
 char standard[64]="PST",summer[64]="PDT";
};
// Gregorian century exceptions do not occur within the time_t range.
inline bool Gmtime(int value,Tm&t){
 if(value<0)return false;
 int rest=value%126230400,year=value/126230400*4+70;bool leap=false;
 if(rest>=31536000){rest-=31536000;++year;if(rest>=31536000){rest-=31536000;++year;if(rest>=31622400){rest-=31622400;++year;}else leap=true;}}
 t.year=year;t.yday=rest/86400;rest%=86400;int month=1;while(Ends[leap][month]<t.yday)++month;
 t.mon=month-1;t.mday=t.yday-Ends[leap][t.mon];t.wday=(value/86400+4)%7;t.hour=rest/3600;rest%=3600;t.min=rest/60;t.sec=rest%60;t.isdst=0;return true;
}
// Shared nth-weekday and absolute transition construction.
inline Transition MakeTransition(bool start,bool relative,int year,int month,int week,int weekday,int day,int hour,int minute,int second,int millis,int dstbias){
 const int leap=(year&3)==0;int yday;
 if(relative){const int first=Ends[leap][month-1]+1;const int wd=(year*365+(year-1)/4+first-25563)%7;
  yday=first+week*7-wd+weekday-(wd<=weekday?7:0);if(week==5&&yday>Ends[leap][month])yday-=7;
 }else yday=Ends[leap][month-1]+day;
 int ms=Add(Mul(Add(Mul(hour,60),minute),60),second);
 if(!start)ms=Add(ms,dstbias);ms=Add(Mul(ms,1000),millis);
 if(!start){if(ms<0){ms=Add(ms,86400000);--yday;}else if(ms>=86400000){ms-=86400000;++yday;}}
 return {year,yday,ms};
}
inline void Transitions(State&s,int year){
 if(s.os_zone){const auto&a=s.zone.daylight;const auto&b=s.zone.standard;
  s.start=MakeTransition(true,!a.year,year,a.month,a.day,a.weekday,a.day,a.hour,a.minute,a.second,a.milliseconds,s.dstbias);
  s.end=MakeTransition(false,!b.year,year,b.month,b.day,b.weekday,b.day,b.hour,b.minute,b.second,b.milliseconds,s.dstbias);
 }else{s.start=MakeTransition(true,true,year,4,1,0,0,2,0,0,0,s.dstbias);s.end=MakeTransition(false,true,year,10,5,0,0,2,0,0,0,s.dstbias);}
}
// The Windows 98 fallback uses the first Sunday in April and last in October.
inline bool IsDst(State&s,const Tm&t){
 if(!s.daylight)return false;if(s.start.year!=t.year||s.end.year!=t.year)Transitions(s,t.year);
 const int a=s.start.day,b=s.end.day,d=t.yday;
 if(a<b){if(d<a||d>b)return false;if(d>a&&d<b)return true;}
 else{if(d<b||d>a)return true;if(d>b&&d<a)return false;}
 const int ms=Mul(Add(Mul(Add(Mul(t.hour,60),t.min),60),t.sec),1000);
 return d==a?ms>=s.start.millis:ms<s.end.millis;
}
// The boundary path retains tm_isdst=0.
inline bool Localtime(State&s,int value,Tm&t){
 if(value<0)return false;
 if(value>259200&&value<2147224447){int local=Add(value,I(0u-U(s.timezone)));if(!Gmtime(local,t))return false;
  if(s.daylight&&IsDst(s,t)){if(!Gmtime(Add(local,I(0u-U(s.dstbias))),t))return false;t.isdst=1;}return true;}
 if(!Gmtime(value,t))return false;
 int v=t.sec;if(IsDst(s,t))v=Add(v,I(0u-U(s.dstbias)));v=Add(v,I(0u-U(s.timezone)));
 t.sec=v%60;if(t.sec<0){t.sec+=60;v=Add(v,-60);}v=Add(v/60,t.min);
 t.min=v%60;if(t.min<0){t.min+=60;v=Add(v,-60);}v=Add(v/60,t.hour);
 t.hour=v%24;if(t.hour<0){t.hour+=24;v=Add(v,-24);}const int days=v/24;
 if(days>0){t.wday=(t.wday+days)%7;t.mday+=days;t.yday+=days;}
 else if(days<0){t.wday=(t.wday+days+7)%7;t.mday+=days;if(t.mday>0)t.yday+=days;else{t.mday+=31;--t.year;t.yday=364;t.mon=11;}}
 return true;
}
inline bool CheckedAdd(int a,int b,int&v){v=Add(a,b);return !((a>=0&&b>=0&&v<0)||(a<0&&b<0&&v>=0));}
inline bool CheckedMul(int a,int b,int&v){v=Mul(a,b);return !a||v/a==b;}
// Month normalization can mutate the caller even on failure.
template<class Initialize> inline int Mktime(State&s,Tm&t,bool local,Initialize initialize){
 int year=t.year;if(year<69||year>139)return -1;
 if(t.mon<0||t.mon>11){year=Add(year,t.mon/12);t.mon%=12;if(t.mon<0){t.mon+=12;--year;}if(year<69||year>139)return -1;}
 int days=Ends[0][t.mon];if(!(year&3)&&t.mon>1)++days;
 days+=year*365+(year-1)/4-25567;int value;
 if(!CheckedAdd(days,t.mday,value)||!CheckedMul(value,24,value)||!CheckedAdd(value,t.hour,value)||!CheckedMul(value,60,value)||!CheckedAdd(value,t.min,value)||!CheckedMul(value,60,value)||!CheckedAdd(value,t.sec,value))return -1;
 Tm result;
 if(local){initialize();value=Add(value,s.timezone);if(!Localtime(s,value,result))return -1;
  if(t.isdst>0||(t.isdst<0&&result.isdst>0)){value=Add(value,s.dstbias);if(!Localtime(s,value,result))return -1;}}
 else if(!Gmtime(value,result))return -1;
 t=result;return value;
}
inline int Mktime(State&s,Tm&t,bool local=true){return Mktime(s,t,local,[]{});}
// Windows 98 uses a zero-padded day of the month.
template<class C> bool Asctime(const Tm&t,C*out){
 constexpr char days[]="SunMonTueWedThuFriSat",months[]="JanFebMarAprMayJunJulAugSepOctNovDec";
 if(t.wday<0||t.wday>6||t.mon<0||t.mon>11)return false; // unsafe original table read is a provider failure
 for(int i=0;i<3;++i){out[i]=days[t.wday*3+i];out[i+4]=months[t.mon*3+i];}out[3]=out[7]=' ';
 auto two=[&](int offset,int value){out[offset]=C(value/10+48);out[offset+1]=C(value%10+48);};
 two(8,t.mday);out[10]=' ';two(11,t.hour);out[13]=':';two(14,t.min);out[16]=':';two(17,t.sec);out[19]=' ';two(20,t.year/100+19);two(22,t.year%100);out[24]='\n';out[25]=0;return true;
}
inline void CopyName(char*out,const char*in,unsigned count){unsigned i=0;for(;i<count&&in[i];++i)out[i]=in[i];for(;i<count;++i)out[i]=0;out[count]=0;}
inline int Atoi(const char*p){while(*p==' '||(*p>=9&&*p<=13))++p;bool negative=*p=='-';if(*p=='+'||*p=='-')++p;U n=0;while(*p>='0'&&*p<='9')n=n*10+U(*p++-'0');return I(negative?0u-n:n);}
// Does not parse POSIX rule suffixes or reset dstbias.
inline bool ParseTz(State&s,const char*p){
 if(!p[0])return true;if(!p[1]||!p[2])return false;CopyName(s.standard,p,3);p+=3;bool negative=*p=='-';if(negative)++p;
 int offset=Mul(Atoi(p),3600);while(*p=='+'||(*p>='0'&&*p<='9'))++p;
 if(*p==':'){++p;offset=Add(offset,Mul(Atoi(p),60));while(*p>='0'&&*p<='9')++p;if(*p==':'){++p;offset=Add(offset,Atoi(p));while(*p>='0'&&*p<='9')++p;}}
 s.timezone=negative?I(0u-U(offset)):offset;s.daylight=int(static_cast<signed char>(*p));if(s.daylight)CopyName(s.summer,p,3);else s.summer[0]=0;return true;
}
inline void Zone(State&s,const Win98Timezone::Zone&z){s.zone=z;s.os_zone=true;s.timezone=Mul(I(z.bias),60);if(z.standard.month)s.timezone=Add(s.timezone,Mul(I(z.standard_bias),60));if(z.daylight.month&&z.daylight_bias){s.daylight=1;s.dstbias=Mul(I(z.daylight_bias-z.standard_bias),60);}else s.daylight=s.dstbias=0;}
// Used by time: DOS/local fields plus CRT bias and the DST flag.
inline int FromLocal(State&s,const Win98Time::SystemTime&t,int dst){
 int year=int(t.year)-1900;if(year<70||year>138||t.month<1||t.month>12)return -1;
 int day=Ends[0][t.month-1]+t.day;if(!(year&3)&&t.month>2)++day;
 int value=Add(Mul(Add(Mul(Add(Mul(year*365+(year-1)/4+day,24),t.hour),60),t.minute),60),s.timezone);value=Add(Add(value,t.second),0x7c558180);
 Tm probe{int(t.second),int(t.minute),int(t.hour),int(t.day),int(t.month)-1,year,0,day,0};
 if(dst==1||(dst==-1&&s.daylight&&IsDst(s,probe)))value=Add(value,s.dstbias);return value;
}
struct Export{const char*name;unsigned argc;};
constexpr Export Exports[]={{"mktime",1},{"gmtime",1},{"localtime",1},{"time",1},{"asctime",1},{"ctime",1},{"_wasctime",1},{"_wctime",1},{"_tzset",0},{"_ftime",1},{"__p__timezone",0},{"__p__daylight",0},{"__p__dstbias",0},{"__p__tzname",0}};
}
