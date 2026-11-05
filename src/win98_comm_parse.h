// SPDX-License-Identifier: GPL-2.0-or-later
// ANSI DCB grammar matching the Win98 KERNEL32 parser.
// Byte offsets describe the 28-byte x86 DCB, without compiler bitfield layout.
#pragma once
#include <string.h>
namespace Win98CommParse {
inline unsigned Get(const unsigned char *p){return p[0]|unsigned(p[1])<<8|unsigned(p[2])<<16|unsigned(p[3])<<24;}
inline void Put(unsigned char *p,unsigned n){for(unsigned i=0;i<4;++i)p[i]=n>>(i*8);}
inline unsigned Upper(unsigned c){return c>='a'&&c<='z'?c-32:c;}
inline bool Match(const char *&p,const char *word){const char *q=p;for(;*word;++word,++q)if(Upper((unsigned char)*q)!=Upper(*word))return false;p=q;return true;}
inline void Spaces(const char *&p){while(*p==' ')++p;}
inline bool Number(const char *&p,unsigned &n){if(*p<'0'||*p>'9')return false;n=0;do{n=n*10+(*p++-'0');}while(*p>='0'&&*p<='9');return true;}
inline bool Parity(const char *&p,unsigned char &value){const char *codes="NOEMS";unsigned c=Upper((unsigned char)*p);for(unsigned i=0;i<5;++i)if(c==unsigned(codes[i])){++p;value=i;return true;}return false;}
inline bool Data(const char *&p,unsigned char &value){unsigned n;if(!Number(p,n)||n<5||n>8)return false;value=n;return true;}
inline bool Stop(const char *&p,unsigned char &value){if(Match(p,"1.5")){value=1;return true;}unsigned n;if(!Number(p,n)||(n!=1&&n!=2))return false;value=n==1?0:2;return true;}
inline bool OnOff(const char *&p,unsigned &value){if(Match(p,"ON")){value=1;return true;}if(Match(p,"OFF")){value=0;return true;}return false;}
inline bool Comma(const char *&p){Spaces(p);if(*p!=',')return false;++p;Spaces(p);return true;}
inline bool Build(const char *text,unsigned char *out,unsigned char *timeouts){
    Put(out,28);unsigned char dcb[28],time[20]{};memcpy(dcb,out,28);if(timeouts)memcpy(time,timeouts,20);
    const char *p=text;
    if(Match(p,"COM")){
        if(*p<'1'||*p>'9')return false;while(*p>='0'&&*p<='9')++p;
        if(*p!=':'&&*p!=' ')return false;Spaces(p);if(*p==':'){++p;Spaces(p);}
    }else if(*p==' ')return false;
    unsigned flags=Get(dcb+8),baud=Get(dcb+4);
    if(strchr(p,',')){
        if(!Number(p,baud))return false;
        switch(baud){case 11:case 30:case 60:baud*=10;break;case 12:case 24:case 48:case 96:baud*=100;break;case 19:baud=19200;break;}
        if(!Comma(p)||!Parity(p,dcb[19])||!Comma(p)||!Data(p,dcb[18])||!Comma(p)||!Stop(p,dcb[20]))return false;
        Spaces(p);if(*p==','){
            ++p;Spaces(p);unsigned flow=Upper((unsigned char)*p);if(flow){++p;Spaces(p);}
            if(flow&&flow!='X'&&flow!='P')return false;
            flags=(flags&~0x300u)|(flow=='X'?0x300u:0);
        }if(*p)return false;
    }else{
        bool has_baud=false,has_stop=false;
        while(*p){Spaces(p);if(!*p)break;unsigned n=0,mask=0,shift=0;
            if(Match(p,"BAUD=")){if(!Number(p,baud))return false;has_baud=true;}
            else if(Match(p,"PARITY=")){if(!Parity(p,dcb[19]))return false;}
            else if(Match(p,"DATA=")){if(!Data(p,dcb[18]))return false;}
            else if(Match(p,"STOP=")){if(!Stop(p,dcb[20]))return false;has_stop=true;}
            else if(Match(p,"TO=")){if(!OnOff(p,n))return false;memset(time,0,20);Put(time+16,n?60000:0);}
            else{
                if(Match(p,"XON=")){mask=0x300;shift=8;}
                else if(Match(p,"ODSR=")){mask=0x40;shift=6;}
                else if(Match(p,"IDSR=")){mask=8;shift=3;}
                else if(Match(p,"OCTS=")){mask=4;shift=2;}
                else if(Match(p,"DTR=")){mask=0x30;shift=4;}
                else if(Match(p,"RTS=")){mask=0x3000;shift=12;}
                else return false;
                if(!OnOff(p,n))return false;
                flags=(flags&~mask)|(mask==0x300?(n?mask:0):n<<shift);
            }
            if(*p&&*p!=' ')return false;
        }
        if(!has_stop)dcb[20]=has_baud&&baud==110?2:0;
    }
    Put(dcb+4,baud);Put(dcb+8,flags);memcpy(out,dcb,28);if(timeouts)memcpy(timeouts,time,20);return true;
}
}
