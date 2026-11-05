// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_font_width.h"
using namespace Win98FontWidth;
struct Memory{unsigned char*p;U Read16(U at){return p[at]|U(p[at+1])<<8;}void Write8(U at,U v){p[at]=v;}void Write16(U at,U v){p[at]=v;p[at+1]=v>>8;}void Write32(U at,U v){for(U i=0;i<4;++i)p[at+i]=v>>(i*8);}};
struct Trace{U*t;void Event(U k,U a=0,U b=0,U c=0){U n=t[0];t[1+n++]=k;t[1+n++]=a;t[1+n++]=b;t[1+n++]=c;t[0]=n;}};
extern "C" U width_convert(const U*a,unsigned char*data,U*trace){
 struct E:Memory,Trace{U available;U CharsetTable(U cs){Event(1,cs);return cs==available||cs==0?0x100:0;}}e{{data},{trace},a[0]};trace[0]=0;
 return ConvertBytes(e,a[1],a[2],0x400,0x600,a[3]);
}
extern "C" U width_run(const U*a,U*trace,U*draw){
 struct E:Trace{const U*a;bool Realize(){Event(1);return a[8];}U Allocate(U n){Event(2,n);return a[9];}
 U Convert(U cs,U n,U src,U dst,U f){Event(3,cs,n,f);Event(4,src,dst);return a[10];}
 U Glyphs(U src,U dst,U n){Event(5,src,dst,n);return a[11];}void Free(U p){Event(6,p);}}e{{trace},a};trace[0]=0;
 RunState s{a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7]};return ConvertRun(e,s,0x1234,a[12],a[13],*draw);
}
extern "C" U width_query(const U*a,unsigned char*data,U*trace){
 struct E:Memory,Trace{const U*a;U converted=0;bool NeedsUpdate(){return a[0];}void Update(){Event(1);}
 U ConvertCharacter(U ch,U&flags){Event(2,ch);flags=a[3];return converted++==a[1]?0:0x1234;}
 U Measure(U lease,U flags){Event(3,lease,flags);return a[2]+converted;}
 bool Mapped(){return a[4];}U MapWidth(U width){Event(4,width);return a[5];}
 void Free(U lease){Event(5,lease);}}e{{data},{trace},a};trace[0]=0;return Widths(e,a[6],a[7],0x100);
}
extern "C" U width_abc(const U*a,U*trace){
 struct E:Trace{const U*a;void Update(){Event(1);}bool Realize(){Event(2);return a[7];}bool RealizeOutline(){Event(3);return a[8];}
 U FontFlags(){return a[6];}U OutlineQuery(){Event(4);return a[9];}}e{{trace},a};trace[0]=0;
 AbcState s{a[0],a[1],a[2],a[3],a[4],a[5],a[6]};return AbcQuery(e,s);
}
extern "C" void width_expand(unsigned char*data,U output,U count){Memory e{data};ExpandAbc(e,output,count);}
