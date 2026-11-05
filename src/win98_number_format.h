// SPDX-License-Identifier: GPL-2.0-or-later
// Native KERNEL32 decimal/grouping and number/currency layout bodies.
#pragma once
#include "win98_system_locale.h"
namespace Win98NumberFormat {
using U = unsigned;
struct Format {
    U digits=2, leading=1, grouping=3, negative=1, positive=0;
    const char *decimal=".", *thousand=",", *currency="$";
};
inline U Length(const char *s) { U n=0; while(s[n])++n; return n; }
// bounded separators cannot contain decimal digits.
inline bool TextValid(const char *s,U maximum,bool required) {
    if(!s)return false;
    U n=0;
    for(;s[n];++n)if(s[n]>='0'&&s[n]<='9')return false;
    return n<maximum&&(!required||n);
}
inline bool Valid(const Format &f,bool currency) {
    return f.digits<=9&&f.leading<=1&&f.grouping<=9&&
        TextValid(f.decimal,4,f.digits!=0)&&TextValid(f.thousand,4,false)&&
        (currency?(f.negative<=15&&f.positive<=3&&TextValid(f.currency,7,false)):f.negative<=4);
}
// Common checks after locale lookup.
template<class E> bool Request(E &e,bool locale,U flags,U value,U format,U output,U capacity) {
    if(!locale||int(capacity)<0||!value||(!output&&capacity)||value==output) {
        e.Error(87);return false;
    }
    if((flags&0x3fffffffu)||(format&&(flags&0x80000000u))) {
        e.Error(1004);return false;
    }
    return true;
}
// and share override/default selection.
// Grouping deliberately parses only the first character, not the full "3;0".
template<class E> U Setting(E &e,U flags,U field,U fallback,U maximum,bool grouping=false) {
    char text[128]{};U value=~0u;
    if(!(flags&0x80000000u)&&e.Override(field,text)) {
        if(grouping)text[1]=0;
        value=Win98SystemLocale::ParseDecimal(text);
        if(int(value)<0||value>maximum)value=~0u;
    }
    if(int(value)<0) {
        e.Locale(field,text);if(grouping)text[1]=0;
        value=Win98SystemLocale::ParseDecimal(text);
        if(int(value)<0||value>maximum)value=fallback;
    }
    return value;
}
template<class E> void Separator(E &e,U flags,U field,U maximum,char *text) {
    if(!(flags&0x80000000u)&&e.Override(field,text)&&TextValid(text,maximum,false))return;
    e.Locale(field,text);
}
// Caller supplies room before output for carry and decoration.
// The original checks the entire fractional input before rendering or rounding.
template<class E> U Decimal(E &e,const char *text,const Format &f,char *&output,bool &zero) {
    const char *end=text;
    while(*end&&*end!='.') {
        if(*end<'0'||*end>'9'){e.Error(87);return 0;}
        ++end;
    }
    if(*end)for(const char *p=end+1;*p;++p)
        if(*p<'0'||*p>'9'){e.Error(87);return 0;}
    while(text<end&&*text=='0')++text;
    const U integers=U(end-text), group=f.grouping;
    const U estimate=integers+(group&&integers?((integers-1)/group)*4:0);
    if(estimate>237){e.Error(87);return 0;}
    char *write=output;
    zero=integers==0;
    if(!integers) { if(f.leading)*write++='0'; }
    else {
        U first=group&&integers>group?integers%group:integers;
        if(!first)first=group;
        while(text<end) {
            for(U n=0;n<first;++n)*write++=*text++;
            if(text<end)for(const char *p=f.thousand;*p;++p)*write++=*p;
            first=group;
        }
    }
    if(f.digits)for(const char *p=f.decimal;*p;++p)*write++=*p;
    if(*text)++text;
    U copied=0;
    while(*text&&copied<f.digits) {
        if(*text!='0')zero=false;
        *write++=*text++;++copied;
    }
    if(write==output)*write++='0';
    if(*text) {
        if(*text>'4') {
            zero=false;
            char *carry=write;
            while(carry>output) {
                --carry;
                if(*carry<'0'||*carry>'9')continue;
                if(*carry=='9'){*carry='0';continue;}
                ++*carry;goto rounded;
            }
            if(group&&integers&&integers%group==0) {
                const char *p=f.thousand+Length(f.thousand);
                while(p>f.thousand)*--output=*--p;
            }
            *--output='1';
        }
    } else while(copied++<f.digits)*write++='0';
rounded:
    *write=0;return U(write-output)+1;
}
inline void Prepend(char *&out,const char *s) {
    const char *end=s+Length(s);while(end>s)*--out=*--end;
}
inline void Append(char *&end,const char *s) { while(*s)*end++=*s++; }
// Negative zero has no sign or parentheses.
template<class E> U Number(E &e,const char *value,const Format &f,const char *sign,char *&out) {
    bool negative=*value=='-';if(negative)++value;
    out+=11;bool zero;
    U count=Decimal(e,value,f,out,zero);if(!count)return 0;
    char *end=out+count-1;
    if(negative&&!zero)switch(f.negative) {
        case 0:*--out='(';*end++=')';break;
        case 2:*--out=' ';Prepend(out,sign);break;
        case 3:Append(end,sign);break;
        case 4:*end++=' ';Append(end,sign);break;
        default:Prepend(out,sign);break;
    }
    *end=0;return U(end-out)+1;
}
// and its 16-entry jump table.
// '$' selects the caller's currency string, '-' the locale negative sign.
template<class E> U Currency(E &e,const char *value,const Format &f,const char *sign,char *&out) {
    bool negative=*value=='-';if(negative)++value;
    out+=18;bool zero;
    U count=Decimal(e,value,f,out,zero);if(!count)return 0;
    char *end=out+count-1;
    const char *prefix="",*suffix="";
    if(negative&&!zero) {
        const char *before[]={"($","-$","$-","$","(","-","","","-","-$ ","","$ ","$ -","","($ ","("};
        const char *after[]={")","","","-","$)","$","-$","$-"," $",""," $-","-","","- $",")"," $)"};
        prefix=before[f.negative];suffix=after[f.negative];
    } else {
        const char *before[]={"$","","$ ",""};
        const char *after[]={"","$",""," $"};
        prefix=before[f.positive];suffix=after[f.positive];
    }
    for(const char *p=prefix+Length(prefix);p>prefix;) {
        char c=*--p;
        if(c=='$')Prepend(out,f.currency);
        else if(c=='-')Prepend(out,sign);
        else *--out=c;
    }
    for(const char *p=suffix;*p;++p) {
        if(*p=='$')Append(end,f.currency);
        else if(*p=='-')Append(end,sign);
        else *end++=*p;
    }
    *end=0;return U(end-out)+1;
}
}
