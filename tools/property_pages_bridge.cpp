// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_property_pages.h"
#include <cstring>
#include <cstdio>
using U=unsigned;
namespace {constexpr U BASE=0x20000000,SOURCE=BASE+0x100,STRINGS=BASE+0x1400,REF=BASE+0x1800,HEAP=BASE+0x2000;
struct E {
 unsigned char memory[65536]{};U trace[4096]{},used=0,next=HEAP,allocations=0,fail=0,callback_result=1,mutation=0;
 U Read(U p){U v=0;memcpy(&v,memory+(p-BASE),4);return v;}void Write(U p,U v){memcpy(memory+(p-BASE),&v,4);}
 void Event(U kind,U a=0,U b=0,U c=0){trace[used++]=kind;trace[used++]=a;trace[used++]=b;trace[used++]=c;}
 U Allocate(U n){U p=0;if(++allocations!=fail){p=next;next+=(n+15)&~15u;memset(memory+(p-BASE),0,n);}Event(1,n,p);return p;}
 void Free(U p){Event(2,p);}
 void Copy(U d,U s,U n){memmove(memory+(d-BASE),memory+(s-BASE),n);}
 U Clone(U source,U encoding){Event(3,source,encoding);U bytes=0;while(memory[source-BASE+bytes]||(encoding==0&&memory[source-BASE+bytes+1]))bytes+=encoding==0?2:1;bytes+=encoding==0?2:1;U p=Allocate(encoding==1?bytes*2:bytes);if(p){if(encoding==1){for(U i=0;i<bytes;++i){unsigned short c=memory[source-BASE+i];memcpy(memory+(p-BASE)+i*2,&c,2);}}else Copy(p,source,bytes);}return p;}
 void Reference(U p,int delta){Write(p,Read(p)+delta);Event(4,p,U(delta),Read(p));}
 U Callback(U proc,U reason,U parameter,U page){Event(5,proc,reason,parameter);if(mutation==1&&reason==0)Write(parameter+28,0xabcdef01);if(mutation==2&&reason==1)Write(parameter+4,Read(parameter+4)&~0x40u);return callback_result;}
 bool LegacyRelease(U page){Event(6,page);return true;}
};}
extern "C" void property_pages(const U*row,U*out){
 E e;e.fail=row[3];e.callback_result=row[4];e.mutation=row[5];
 U source[12]={row[1],row[2],0x555,17,18,19,0x100600,0x12345678,0x100200,REF,20,21};
 for(U i=0;i<12;++i)e.Write(SOURCE+i*4,source[i]);e.Write(REF,0xffffffffu);
 for(U i=0;i<5;++i){U offset=i==0?12:i==1?16:i==2?20:i==3?40:44;
  if(row[6]&(1u<<i)){U p=STRINGS+i*64;e.Write(SOURCE+offset,p);const char*s=i==0?"PAGE":i==1?"ICON":i==2?"Title":i==3?"Header":"Subtitle";U n=strlen(s)+1;for(U j=0;j<n;++j){e.memory[p-BASE+(row[0]?j:j*2)]=s[j];}}
 }
 U handle=Win98PropertyPages::Create(e,SOURCE,row[0]);out[0]=handle;out[1]=handle?Win98PropertyPages::Destroy(e,handle):0;out[2]=e.used;out[3]=e.next-HEAP;
 memcpy(out+4,e.trace,sizeof e.trace);memcpy(reinterpret_cast<unsigned char*>(out+4100),e.memory,sizeof e.memory);
}
#ifdef PROPERTY_PAGES_CORPUS_MAIN
int main(int argc,char**argv){if(argc!=2)return 2;FILE*f=fopen(argv[1],"rb");if(!f)return 2;U row[7],expected[20484],got[20484],count=0;while(fread(row,sizeof row,1,f)==1){if(fread(expected,sizeof expected,1,f)!=1)return 2;memset(got,0,sizeof got);property_pages(row,got);if(memcmp(got,expected,sizeof got)){fprintf(stderr,"row%u failed\n",count);return 1;}++count;}fclose(f);printf("PROPERTY PAGE CORPUS %u PASS\n",count);}
#endif
