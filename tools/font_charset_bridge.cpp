// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_font_charset.h"
using namespace Win98FontCharset;
struct Memory{unsigned char*p;U Read8(U at){return p[at];}U Read16(U at){return p[at]|U(p[at+1])<<8;}
 U Read32(U at){return Read16(at)|(Read16(at+2)<<16);}void Write32(U at,U v){for(U i=0;i<4;++i)p[at+i]=v>>(i*8);}};
struct Trace{U*t;void Event(U k,U a=0,U b=0,U c=0){U n=t[0];t[1+n++]=k;t[1+n++]=a;t[1+n++]=b;t[1+n++]=c;t[0]=n;}};
extern "C" U charset_face(const U*a,unsigned char*data,U*trace){
 struct E:Memory,Trace{const U*a;void Update(){Event(1);}bool Realize(){Event(2);return a[8];}bool KerningPairs(){Event(3);return a[9];}}e{{data},{trace},a};trace[0]=0;
 Face f{a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7]};return a[10]?LanguageInfo(e,f):TextCharset(e,f,a[11]);
}
extern "C" U charset_signature(const U*a,unsigned char*data,U*trace){
 struct E:Memory,Trace{const U*a;bool Writable(U p,U n){Event(1,p,n);return a[0];}
 U Charset(U dc){Event(2,dc);return a[1];}U CharsetReference(U dc,U&ref){Event(3,dc);ref=a[2];return a[1];}
 U Table(U font,U tag){Event(4,font,tag);return a[3];}}e{{data},{trace},a};trace[0]=0;return Signature(e,a[4],a[5]);
}
extern "C" U charset_table(const U*a,unsigned char*data,U*trace){
 struct E:Memory,Trace{const U*a;U FontRange(U font,U off,U n){Event(1,font,off,n);return off==12?a[0]:a[1];}}e{{data},{trace},a};trace[0]=0;return FindTable(e,0x100,a[2]);
}
extern "C" U charset_translate(const U*a,unsigned char*data,const U*tables,U*trace){
 struct E:Memory,Trace{const U*a,*tables;bool Writable(U p,U n){Event(1,p,n);return a[0];}
 bool Readable(U p,U n){Event(2,p,n);return a[1];}U Entry(U table,U index){return tables[table*14+index];}
 bool LocaleSignature(U locale,U*out){Event(3,locale,0x58,0x22);for(U i=0;i<8;++i)out[i]=a[5+i];return a[2];}}e{{data},{trace},a,tables};trace[0]=0;return Translate(e,a[3],0x600,a[4]);
}
