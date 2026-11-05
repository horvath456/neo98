// SPDX-License-Identifier: GPL-2.0-or-later
// KERNEL32 4.10.2222 ANSI date/time formatting.
#pragma once
#include "win98_time.h"
namespace Win98DateTime {
using U=unsigned;using Time=Win98Time::SystemTime;
struct Output {
    char*data;U capacity,size=0;bool overflow=false;
    void Byte(char c){if(size<capacity)data[size]=c;else overflow=true;++size;}
    void Text(const char*s){if(s)while(*s)Byte(*s++);}
    void Number(U n,U width){char b[16];U count=0;do{b[count++]=char('0'+n%10);n/=10;}while(n);while(count<width)b[count++]='0';while(count)Byte(b[--count]);}
    void Back(U n){size=n;}
};
// date fields are not examined.
inline bool ValidTime(const Time&t){return t.hour<24&&t.minute<60&&t.second<60&&t.milliseconds<1000;}
// use original Gregorian conversion, write back weekday only.
inline bool ValidDate(Time&t){Time d=t;d.hour=d.minute=d.second=d.milliseconds=0;Win98Time::Q q;if(!Win98Time::SystemToFile(d,q))return false;Win98Time::FileToSystem(q,d);t.weekday=d.weekday;return true;}
inline U FormatFlags(bool date,U flags,bool custom){
    if(flags&0x3ffffff0u)return 1004;
    if(custom&&(flags&(date?0x8000000bu:0x80000000u)))return 1004;
    if(date&&!custom){U choice=flags&11;if(choice!=0&&choice!=1&&choice!=2&&choice!=8)return 1004;}
    return 0;
}
inline U EnumFlags(bool date,U flags){if(!date)return flags&0xbfffffffu?1004:0;U f=flags&0xbfffffffu;return f==1||f==2||f==8?0:1004;}
// /. Doubled quotes inside a quoted run emit one quote;
// an opening quote immediately followed by a closing quote emits nothing.
inline void Quote(const char*&p,Output&o){while(*p){if(*p=='\''){++p;if(*p!='\'')break;}o.Byte(*p++);}}
// Last-token rollback deliberately removes preceding literals.
template<class E> U TimeFormat(E&e,const Time&t,U flags,const char*format,Output&o){
    U last=0;const char*p=format;
    while(*p){char c=*p++;if(c=='\''){Quote(p,o);continue;}
        if(c!='h'&&c!='H'&&c!='m'&&c!='s'&&c!='t'){o.Byte(c);continue;}
        U run=1;while(*p==c){++p;++run;}
        if((c=='m'&&(flags&1))||(c=='s'&&(flags&3))){o.Back(last);continue;}
        if(c=='t'){
            if(flags&4){o.Back(last);while(*p&&*p!='h'&&*p!='H'&&*p!='m'&&*p!='s')++p;continue;}
            const char*marker=e.Info(t.hour<12?0x28:0x29);if(!marker||!*marker){o.Back(last);continue;}
            if(run==1)o.Byte(*marker);else o.Text(marker);
        }else{U value=c=='m'?t.minute:c=='s'?t.second:t.hour;if(c=='h'&&!(flags&8)){value%=12;if(!value)value=12;}o.Number(value,run==1?1:2);}
        last=o.size;
    }o.Byte(0);return o.size;
}
// Gregorian branch, including the numeric-day/genitive probe.
template<class E> U DateFormat(E&e,const Time&t,const char*format,Output&o){
    bool day_before=false;const char*p=format;
    while(*p){char c=*p++;if(c=='\''){Quote(p,o);continue;}
        if(c!='M'&&c!='d'&&c!='y'&&c!='g'){o.Byte(c);continue;}
        U run=1;while(*p==c){++p;++run;}
        if(c=='M'){
            if(run<3)o.Number(t.month,run);
            else{bool genitive=day_before;if(!genitive){const char*q=p;while(*q&&*q!='g'&&*q!='y'){if(*q++=='d'){U n=1;while(*q=='d'){++q;++n;}genitive=n<3;break;}}}o.Text(e.Month(t.month,run==3,genitive));}day_before=false;
        }else if(c=='d'){
            if(run<3){o.Number(t.day,run);day_before=true;}
            else{o.Text(e.Info((run==3?0x31:0x2a)+(t.weekday+6)%7));day_before=false;}
        }else if(c=='y'){o.Number(run<3?t.year%100:t.year,run==1?1:2);day_before=false;}
        else{e.Era(o,t);day_before=false;}
    }o.Byte(0);return o.size;
}
// false and any non-one callback value stop successfully.
template<class E> U Enumerate(E&e,const char*const*formats,U count){for(U i=0;i<count;++i){U result=0;if(!e.Callback(formats[i],result))return 0;if(result!=1)return 1;}return 1;}
}
