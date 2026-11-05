/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_gdi_wide_text.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
using U=unsigned;
struct E{
 const U*a;U meta;std::vector<U>trace;
 void Event(U n,U x=0,U y=0,U z=0){trace.insert(trace.end(),{n,x,y,z});}
 bool NeedsUpdate(){return a[0];}void Update(){Event(1);}bool InvalidHandle(){return a[1];}
 U Signature(){return a[2];}U Metafile(){return meta;}void SetMetafile(U v){meta=v;}
 U Record(U n){Event(2,n);return a[4];}
 U Convert(U text,U count,U flags,U&out){Event(3,text,count,flags);out=a[6];return a[5]?0x1234:0;}
 U Draw(U text,U count,U flags){Event(4,text,count,flags);Event(5,meta);return a[7];}
 void Free(U text){Event(6,text);}
 U Measure(U text,U count,U flags){Event(7,text,count,flags);return a[7];}
 bool Mapped(){return a[8];}U Map(U value,U axis){Event(8,value,axis);return a[9+axis];}
};
extern "C" U wide_core(U mode,const U*a,U*out){E e{a,a[3],{}};U result=0;
 if(mode==0)result=Win98GdiWideText::Draw(e,a[11],a[12],a[13]);
 else if(mode==1)result=Win98GdiWideText::Extent(e,a[11],a[12]);
 else{U packed=0;result=Win98GdiWideText::ExtentPoint(e,a[11],a[12],packed);e.Event(9,packed);}
 out[0]=e.meta;out[1]=e.trace.size();for(U i=0;i<e.trace.size();++i)out[i+2]=e.trace[i];return result;
}
#ifdef WIDE_SANITIZER
int main(int argc,char**argv){if(argc!=2)return 2;FILE*f=fopen(argv[1],"rb");if(!f)return 3;U mode,a[14],wanted,n;unsigned cases=0;while(fread(&mode,4,1,f)==1){if(fread(a,4,14,f)!=14||fread(&wanted,4,1,f)!=1||fread(&n,4,1,f)!=1||n>256)return 4;U expected[256],actual[256]{};if(fread(expected,4,n,f)!=n)return 5;if(wide_core(mode,a,actual)!=wanted)return 6;for(U i=0;i<n;++i)if(expected[i]!=actual[i])return 7;++cases;}fclose(f);printf("PASS %u original wide-text cases\n",cases);}
#endif
