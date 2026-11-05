/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_shell_file_info.h"
#include <cstring>
using U=unsigned;static constexpr U B=0x30000000,P=B+0x2000,T=P+8,D=B+0x4000,F=B+0x4100;
struct E {unsigned char*m;U*c,*trace,n=0;void event(U op,U a=0,U b=0,U z=0){trace[n++]=op;trace[n++]=a;trace[n++]=b;trace[n++]=z;}
 U Get(U p){U v;memcpy(&v,m+p-B,4);return v;}void Put(U p,U v){memcpy(m+p-B,&v,4);}U GetByte(U p){return m[p-B];}void Byte(U p,U v){m[p-B]=v;}
 U Desktop(){event(1,1);return D;}void NullPath(){event(2);}U ExecutableType(U p){event(3,p);return c[0];}bool IsRoot(U p){event(4,p);return c[1];}bool Relative(U p){event(5,p);return c[2];}U Absolute(U p){event(6,p);return B+0x1200;}
 U Parse(U p,bool synthetic){event(7,p,synthetic);return c[3]?0:P;}U Last(U p){event(8,p);return c[4]?P:T;}U Type(U p){return GetByte(p+2);}void Type(U p,U t){Byte(p+2,t);}void TypeName(U p,U out,U n){event(9,p,n);strcpy(reinterpret_cast<char*>(m+out-B),"File type");}
 void AddRef(U p){event(10,p);}int Bind(U d,U p,U t,U&f){event(11,d,p,t);f=F;return c[5]?-1:0;}
 int Attributes(U f,U t,U out){event(12,f,t,Get(out));if(c[6])return -1;Put(out,Get(out)&c[7]);return 0;}
 int Extractor(U f,U t,U&x){event(13,f,t);x=B+0x4200;return c[8]?-1:0;}void Location(U x,U out,U n,U index,U&flags){event(14,x,n);strcpy(reinterpret_cast<char*>(m+out-B),c[9]&16?"*":"fixture.dll");Put(index,7);flags=c[9];}
 void Release(U p){event(15,p);}U LargeList(){return c[10];}U SmallList(){return c[11];}void InitializeLists(){event(16);c[10]=0x600;c[11]=0x601;}
 U IconIndex(U f,U t,bool open,U out){event(17,f,t,open);if(open)Put(out,14);return 13;}U Metric(U i){event(18,i);return i==49||i==50?16:32;}
 U Icon(U list,U idx,U flags){event(19,list,idx,flags);return c[12]?0:0x700;}U ResizeIcon(U icon,U x,U y){event(20,icon,x,y);return 0x701;}void DisplayName(U f,U t,U out,U n){event(21,f,t,n);strcpy(reinterpret_cast<char*>(m+out-B),"Fixture.txt");}void FreePidl(U p){event(22,p);}
};
extern "C" U shell_file_info(U*args,unsigned char*mem,U*config,U*trace,U*count){E e{mem,config,trace};U r=Win98ShellFileInfo::Query(e,args[0],args[1],args[2],args[3],args[4]);*count=e.n;return r;}
