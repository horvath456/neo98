/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_crt_decimal.h"
using namespace Win98CrtDecimal;
struct E{const unsigned char*s;U decimal;U Byte(U p){return s[p];}bool Digit(U c){return c>='0'&&c<='9';}bool Space(U c){return c==' '||(c>=9&&c<=13);}U Decimal(){return decimal;}};
extern "C" void decimal_scan(const unsigned char*s,U sep,unsigned short*out,U*end,U*flags){E e{s,sep};auto v=Parse(e,0);for(U i=0;i<6;++i)out[i]=v.number.w[i];*end=v.end;*flags=v.flags;}
extern "C" void decimal_convert(const unsigned char*s,U sep,U atof,U*out){E e{s,sep};auto v=Convert(e,0,atof);out[0]=U(v.bits);out[1]=U(v.bits>>32);out[2]=v.end;out[3]=v.range;}
extern "C" void decimal_digits(const unsigned char*d,U n,unsigned short*out){auto v=Digits(d,n);for(U i=0;i<6;++i)out[i]=v.w[i];}
extern "C" void decimal_scale(unsigned short*a,int exponent){Extended v;for(U i=0;i<6;++i)v.w[i]=a[i];Scale(v,exponent);for(U i=0;i<6;++i)a[i]=v.w[i];}
extern "C" void decimal_multiply(unsigned short*a,const unsigned short*b){Extended v,w;for(U i=0;i<6;++i){v.w[i]=a[i];w.w[i]=b[i];}Multiply(v,w);for(U i=0;i<6;++i)a[i]=v.w[i];}
extern "C" U decimal_pack(const unsigned short*a,U*out){Extended v;for(U i=0;i<6;++i)v.w[i]=a[i];auto p=Double(v);out[0]=U(p.bits);out[1]=U(p.bits>>32);return p.status;}
