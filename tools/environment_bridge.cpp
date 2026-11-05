// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_environment_public.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using U=unsigned;
namespace W=Win98Environment;
namespace {
constexpr U Base=0x30000000,Size=0x100000,Region=Base+0x40000,Temporary=Base+0xc0000,Local=0x60000000;
unsigned char memory[Size];
struct Environment {
 U block=0, committed=0, error=0, failure=0, nth=0, counts[4]{}, local[25]{};
 unsigned char permissions[Size/4096];
 Environment(){memset(permissions,3,sizeof permissions);}
 std::vector<U>trace;
 bool ProbeRead(U p,U &v){if(p<Base||p-Base>=Size||!(permissions[(p-Base)/4096]&1))return false;v=Read8(p);return true;}
 bool ProbeWrite(U p){U v;if(!ProbeRead(p,v)||!(permissions[(p-Base)/4096]&2))return false;Write8(p,v);return true;}
 unsigned char*Byte(U p){if(p>=Local && p-Local<100)return reinterpret_cast<unsigned char*>(local)+p-Local;assert(p>=Base && p-Base<Size);return memory+(p-Base);}
 U Read8(U p){return *Byte(p);} void Write8(U p,U v){*Byte(p)=v;}
 U Read32(U p){U v=0;for(U i=0;i<4;++i)v|=Read8(p+i)<<(i*8);return v;}
 void Write32(U p,U v){for(U i=0;i<4;++i)Write8(p+i,v>>(i*8));}
 void Event(U kind,U a=0,U b=0,U c=0,U d=0){const U values[]={kind,a,b,c,d};trace.insert(trace.end(),values,values+5);}
 U CurrentProcess(){return 0x31000100;} U ProcessBlock(){return block;}
 void Lock(){Event(1);}void Unlock(){Event(2);}void Error(U code){error=code;Event(3,code);}
 bool Fails(U kind){return ++counts[kind]==nth && failure==kind;}
 U VirtualAllocate(U p,U n,U flags,U protection){
  assert(protection==4);U result=0;
  if(flags==0x2000 || flags==0x08002000){assert(!p);if(!Fails(1)){result=Region;committed=0;}}
  else {assert(flags==0x1000 && p==Region);if(!Fails(2)){assert(n>=committed && n<=0x80000);memset(Byte(Region+committed),0,n-committed);committed=n;result=Region;}}
  if(!result)error=8;Event(4,p,n,flags,result);return result;
 }
 U VirtualRelease(U p,U n,U flags){assert(p==Region && !n && flags==0x8000);memset(Byte(p),0xdd,committed);committed=0;Event(5,p,n,flags,1);return 1;}
 U LocalName(){return Local;}
 U AllocateTemporary(U n){const U result=Fails(3)?0:Temporary;assert(n<0x30000);if(result)memset(Byte(result),0xcd,n);else error=8;Event(6,n,result);return result;}
 void FreeTemporary(U p){assert(p==Temporary);Event(7,p);}
} e;
}
extern "C" void environment_reset(const unsigned char*bytes){memcpy(memory,bytes,Size);e=Environment{};}
extern "C" const unsigned char*environment_memory(){return memory;}
extern "C" const U*environment_trace(U*size){*size=e.trace.size();return e.trace.data();}
extern "C" void environment_run(const U*a,U*out){
 e.failure=a[4];e.nth=a[5];e.error=a[6];memset(e.counts,0,sizeof e.counts);e.trace.clear();U r=0;
 switch(a[0]){
 case 0:r=W::GetStrings(e);break;case 1:r=W::FreeStrings(e,a[1]);break;
 case 2:r=W::GetPublic(e,a[1],a[2],a[3]);break;case 3:r=W::SetPublic(e,a[1],a[2]);break;
 case 4:r=W::ExpandPublic(e,a[1],a[2],a[3]);break;
 case 5:r=W::CreateBlock(e,a[1],a[2]);e.block=r;break;
 case 6:r=W::ValidBlock(e,a[1]);break;case 7:r=W::Capacity(e,a[1]);break;
 case 8:r=W::Grow(e,a[1],a[2]);break;case 9:r=W::BlockLength(e,a[1],a[2]);break;
 case 10:r=W::Match(e,a[1],a[2]);break;
 case 11:{U name=e.Read32(a[1]),value=0;r=W::Find(e,name,value,a[3]);e.Write32(a[1],name);e.Write32(a[2],value);break;}
 case 12:r=W::SetBlock(e,a[1],a[2],a[3]);break;case 13:r=W::ReplaceBlock(e,a[1]);break;
 case 14:r=W::Upper(e,a[1]);break;
 case 15:case 16:case 17:case 18:case 19:e.Error(120);r=0;break;
 case 20:e.Write32(a[1],a[2]);break;
 case 21:assert(a[1]>=Base && a[1]-Base<Size && !(a[1]&4095));e.permissions[(a[1]-Base)/4096]=a[2];break;
 default:assert(false);
 }
 out[0]=r;out[1]=e.error;out[2]=e.block;out[3]=e.committed;
 for(U i=1;i<4;++i)out[3+i]=e.counts[i];
}
#ifdef ENVIRONMENT_STANDALONE
void ReadDelta(FILE*f,unsigned char*target){U count;assert(fread(&count,4,1,f)==1);assert(count<=Size/4096);
 for(U i=0;i<count;++i){U offset;assert(fread(&offset,4,1,f)==1);assert(!(offset&4095)&&offset<=Size-4096);assert(fread(target+offset,1,4096,f)==4096);}}
int main(int argc,char**argv){assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);U sequences,calls=0;assert(fread(&sequences,4,1,f)==1);
 for(U seq=0;seq<sequences;++seq){U n;assert(fread(&n,4,1,f)==1);memset(memory,0xcc,Size);ReadDelta(f,memory);e=Environment{};std::vector<unsigned char>expected(memory,memory+Size);
  for(U i=0;i<n;++i){U a[8],want[7],got[7],trace_n;
   assert(fread(a,4,8,f)==8 && fread(want,4,7,f)==7);ReadDelta(f,expected.data());assert(fread(&trace_n,4,1,f)==1);
   std::vector<U>trace(trace_n);assert(fread(trace.data(),4,trace_n,f)==trace_n);environment_run(a,got);
   if(memcmp(got,want,sizeof got)||memcmp(memory,expected.data(),Size)||trace!=e.trace){fprintf(stderr,"environment mismatch %u/%u op%u\n",seq,i,a[0]);return 1;}++calls;
  }
 }
 fclose(f);printf("ENVIRONMENT ASAN/UBSAN PASS %u sequences/%u calls\n",sequences,calls);return 0;}
#endif
