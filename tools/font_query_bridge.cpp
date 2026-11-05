// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_font_query.h"
using namespace Win98FontQuery;
struct Fixture{Face face;U requested,alias,charset,physical_atom,version,special,realize,atom_result;};
extern "C" U font_face(Fixture*f,U count,U output,unsigned char*data,U*trace){
 struct E{Fixture&f;unsigned char*p;U*t;void Event(U k,U a=0,U b=0,U c=0){U n=t[0];t[1+n++]=k;t[1+n++]=a;t[1+n++]=b;t[1+n++]=c;t[0]=n;}
 bool Realize(){Event(1);return f.realize;}U FindAtom(U p){Event(2,p);return p==f.face.logical?f.requested:f.physical_atom;}
 void Alias(U a,U&alias,U&charset){Event(3,a);alias=f.alias;charset=f.charset;}
 bool SpecialAtom(U a){return a==f.special;}U ExpectedVersion(){Event(4);return f.version;}
 U AtomName(U a,U n,U out){Event(5,a,n,out);return f.atom_result;}
 U Read8(U at){return p[at];}void Write8(U at,U v){p[at]=v;}
 }e{*f,data,trace};trace[0]=0;return TextFace(e,f->face,count,output);
}
extern "C" U font_family(U atom,U charset,U requested,U all,const U*seen,U n){return FamilyCandidate(atom,charset,requested,all,seen,n);}
extern "C" U font_enum(const U*a,unsigned char*data,U*trace){
 struct E{const U*a;unsigned char*p;U*t;void Event(U k,U x=0,U y=0,U z=0){U n=t[0];t[1+n++]=k;t[1+n++]=x;t[1+n++]=y;t[1+n++]=z;t[0]=n;}
 U Snapshot(U dc,U face,U cs,U mode,U&count,U&rows,U&lease){Event(1,dc,face,cs);Event(2,mode);count=a[7];rows=0x100;lease=0x1234;return a[6];}
 U Read32(U at){return U(p[at])|(U(p[at+1])<<8)|(U(p[at+2])<<16)|(U(p[at+3])<<24);}
 U Callback(U cb,U lf,U tm,U type,U param){Event(3,cb,lf,tm);Event(4,type,param);const U index=(lf-0x100)/264;return index==a[8]?0:a[9]+index;}
 void Release(U lease){Event(5,lease);}
 }e{a,data,trace};trace[0]=0;return Enumerate(e,a[0],a[1],a[2],a[3],a[4],a[5]);
}

extern "C" void font_expand(unsigned char*data,U output,U count){
 struct E{unsigned char*p;U Read16(U at){return U(p[at])|(U(p[at+1])<<8);}void Write32(U at,U v){for(U i=0;i<4;++i)p[at+i]=v>>(i*8);}}e{data};ExpandWidths(e,output,count);
}
