// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_shell_registry.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using U=unsigned;
namespace W=Win98ShellRegistry;
namespace {
constexpr U Base=0x30000000,Size=65536,Local=0x60000000,Value=Base+0x2400,Expanded=Base+0x2800;
unsigned char memory[Size];
using ReadCallback=U(*)(U);using WriteCallback=void(*)(U,U);ReadCallback read_callback=nullptr;WriteCallback write_callback=nullptr;
struct Environment {
 U config[16]{},count[13]{},last_error=0,scratch_used=0;
 unsigned char local[8192]{};
 std::vector<U>trace;
 unsigned char*Byte(U p){if(p>=Local && p-Local<sizeof local)return local+(p-Local);assert(p>=Base && p-Base<Size);return memory+(p-Base);}
 U Read8(U p){if(read_callback && !(p>=Base&&p-Base<Size))return read_callback(p);return *Byte(p);}
 void Write8(U p,U v){if(write_callback && !(p>=Base&&p-Base<Size)){write_callback(p,v);return;}*Byte(p)=v;}
 U Read32(U p){U v=0;for(U i=0;i<4;++i)v|=Read8(p+i)<<(i*8);return v;}void Write32(U p,U v){for(U i=0;i<4;++i)Write8(p+i,v>>(i*8));}
 struct Scratch{Environment&e;U address,bytes;Scratch(Environment&v,U n):e(v),address(Local+v.scratch_used),bytes((n+3)&~3u){e.scratch_used+=bytes;assert(e.scratch_used<sizeof e.local);memset(e.Byte(address),0,bytes);}~Scratch(){e.scratch_used-=bytes;}};
 U Failure(U kind){return ++count[kind]==config[10] && config[9]==kind?config[11]:0;}
 U TextHash(U p){if(!p)return 0;U h=2166136261;for(U c;(c=Read8(p++));)h=(h^c)*16777619;return h;}
 void Event(U kind,U a=0,U b=0,U c=0,U d=0,U f=0,U g=0){U v[]={kind,a,b,c,d,f,g};trace.insert(trace.end(),v,v+7);}
 U LastError(){return last_error;}
 U CharPrev(U a,U b){return b>a?b-1:a;}
 U EmptyString(){return Base+0x2f00;}
 U BadWrite(U p,U n){U result=Failure(12)||p<Base||p-Base>Size||n>Size-(p-Base);Event(12,p,n,result);return result;}
 U LocalAllocate(U flags,U bytes){assert(flags==0x40);U result=0;if(!Failure(1)){
   for(U i=0;i<32;++i)if(!Read32(Base+0xf000+i*8)){
    U p=Read32(Base+0xeff4);if(!p)p=Base+0x4000;
    if(bytes>0x8000 || p+bytes>Base+0xd000)break;
    Write32(Base+0xeff4,(p+bytes+15)&~15u);Write32(Base+0xf000+i*8,p);Write32(Base+0xf004+i*8,bytes);
    memset(Byte(p),0,bytes);result=p;break;
   }
  }if(!result)last_error=8;Event(1,flags,bytes,result);return result;}
 U LocalFree(U p){bool found=false;for(U i=0;i<32;++i)if(Read32(Base+0xf000+i*8)==p){U n=Read32(Base+0xf004+i*8);memset(Byte(p),0xdd,n);Write32(Base+0xf000+i*8,0);Write32(Base+0xf004+i*8,0);found=true;break;}assert(found);Event(2,p);return 0;}
 bool Key(U key){return key==W::CurrentUser||key==W::LocalMachine||(key>=0x5000&&(key-0x5000)/4<256&&Read32(Base+0xe000+key-0x5000));}
 U NewKey(){U serial=Read32(Base+0xeff0);assert(serial<256);Write32(Base+0xeff0,serial+1);Write32(Base+0xe000+serial*4,1);return 0x5000+serial*4;}
 U RegOpen(U root,U name,U access,U out){U result=Failure(2);if(!result&&!Key(root))result=6;U h=0;if(!result){h=NewKey();Write32(out,h);}Event(3,root,TextHash(name),access,result,h);return result;}
 U RegCreate(U root,U name,U klass,U access,U out,U disposition){U result=Failure(3);if(!result&&!Key(root))result=6;U h=0;if(!result){h=NewKey();Write32(out,h);if(disposition)Write32(disposition,1);}Event(4,root,TextHash(name),access,result,h,klass?1:0);return result;}
 U RegClose(U key){U result=Failure(5);if(!result&&!Key(key))result=6;if(!result&&key<W::CurrentUser)Write32(Base+0xe000+key-0x5000,0);Event(5,key,result);return result;}
 U RegQuery(U key,U name,U reserved,U type,U data,U size){assert(!reserved);U result=Failure(4),capacity=size?Read32(size):0,n=config[13];
  if(!result&&!Key(key))result=6;
  // This explicit registry provider always publishes metadata, including on
  // injected errors; it avoids attributing uninitialized SHLWAPI stack bytes.
  if(type)Write32(type,config[12]);if(size)Write32(size,n);
  if(!result&&data){if(!size)result=87;else if(capacity<n)result=234;else W::Copy(*this,data,Value,n);}
  Event(6,key,TextHash(name),data?capacity:0,(type?1:0)|(data?2:0)|(size?4:0),result,n);return result;}
 U RegSet(U key,U name,U reserved,U type,U data,U size){assert(!reserved);U result=Failure(4);if(!result&&!Key(key))result=6;U hash=2166136261;for(U i=0;i<size;++i)hash=(hash^Read8(data+i))*16777619;Event(7,key,TextHash(name),type,size,result,hash);return result;}
 U RegDeleteValue(U key,U name){U result=Failure(6);if(!result&&!Key(key))result=6;Event(8,key,TextHash(name),result);return result;}
 U RegDeleteKey(U key,U name){U result=Failure(7);if(!result&&!Key(key))result=6;Event(9,key,TextHash(name),result);return result;}
 U RegEnumKey(U key,U index,U name,U length){U result=Failure(8),capacity=length?Read32(length):0;if(!result&&!Key(key))result=6;if(!result){if(capacity<6||!name)result=234;else {W::StringCopy(*this,name,Base+0x2a00);Write32(length,5);}}Event(10,key,index,capacity,result);return result;}
 U RegEnumValue(U key,U index,U name,U length,U type,U data,U size){U result=Failure(9),capacity=length?Read32(length):0,n=size?Read32(size):0;if(!result&&!Key(key))result=6;if(type)Write32(type,config[12]);if(size)Write32(size,config[13]);if(!result){if(capacity<6||!name)result=234;else{W::StringCopy(*this,name,Base+0x2a00);Write32(length,5);if(data){if(n<config[13])result=234;else W::Copy(*this,data,Value,config[13]);}}}Event(11,key,index,capacity,n,result,(type?1:0)|(data?2:0)|(size?4:0));return result;}
 U RegInfo(U key,U children,U max_child,U values,U max_value){U result=Failure(10);if(!result&&!Key(key))result=6;if(!result){if(children)Write32(children,config[14]&255);if(values)Write32(values,(config[14]>>8)&255);if(max_child)Write32(max_child,9);if(max_value)Write32(max_value,7);}Event(13,key,(children?1:0)|(max_child?2:0)|(values?4:0)|(max_value?8:0),result);return result;}
 U Expand(U source,U output,U capacity){U result=Failure(11);U n=W::Length(*this,Expanded)+1;if(result){last_error=result;result=0;}else{result=n;if(capacity>=n)W::Copy(*this,output,Expanded,n);else if(capacity)Write8(output,0);}Event(14,TextHash(source),capacity,result);return result;}
}e;
U Resolve(U p){return (p&0xffff0000)==0xf1000000?e.Read32(Base+(p&65535)):p;}
}
extern "C" void shell_registry_reset(const unsigned char*p){memcpy(memory,p,Size);e=Environment{};}
extern "C" const unsigned char*shell_registry_memory(){return memory;}
extern "C" const U*shell_registry_trace(U*n){*n=e.trace.size();return e.trace.data();}
extern "C" void shell_registry_callbacks(ReadCallback read,WriteCallback write){read_callback=read;write_callback=write;}
extern "C" void shell_registry_provider_begin(const U*input){memcpy(e.config,input,sizeof e.config);memset(e.count,0,sizeof e.count);e.trace.clear();e.last_error=input[15];}
extern "C" U shell_registry_provider(U kind,const U*a){switch(kind){
 case 0:return e.LocalAllocate(a[0],a[1]);case 1:return e.LocalFree(a[0]);case 2:return e.BadWrite(a[0],a[1]);
 case 3:return e.RegOpen(a[0],a[1],a[2],a[3]);case 4:return e.RegCreate(a[0],a[1],a[2],a[3],a[4],a[5]);
 case 5:return e.RegClose(a[0]);case 6:return e.RegQuery(a[0],a[1],a[2],a[3],a[4],a[5]);
 case 7:return e.RegSet(a[0],a[1],a[2],a[3],a[4],a[5]);case 8:return e.RegDeleteValue(a[0],a[1]);case 9:return e.RegDeleteKey(a[0],a[1]);
 case 10:return e.RegEnumKey(a[0],a[1],a[2],a[3]);case 11:return e.RegEnumValue(a[0],a[1],a[2],a[3],a[4],a[5],a[6]);
 case 12:return e.RegInfo(a[0],a[1],a[2],a[3],a[4]);case 13:return e.Expand(a[0],a[1],a[2]);case 14:return e.LastError();
 default:assert(false);return 0;}}
extern "C" void shell_registry_run(const U*input,U*out){
 memcpy(e.config,input,sizeof e.config);memset(e.count,0,sizeof e.count);e.trace.clear();e.last_error=input[15];U a[8];for(U i=0;i<8;++i)a[i]=Resolve(input[i+1]);U r=0;
 switch(input[0]){
 case 0:r=W::Open(e,a[0],a[1],a[2],a[3],a[4]);break;
 case 1:r=W::Create(e,a[0],a[1],a[2],a[3],a[4]);break;
 case 2:r=W::Query(e,a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7]);break;
 case 3:r=W::Get(e,a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7]);break;
 case 4:r=W::Set(e,a[0],a[1],a[2],a[3],a[4],a[5]);break;
 case 5:r=W::Write(e,a[0],a[1],a[2],a[3],a[4],a[5]);break;
 case 6:r=W::Delete(e,a[0],a[1],a[2],false);break;
 case 7:r=W::Delete(e,a[0],a[1],a[2],true);break;
 case 8:r=W::Close(e,a[0]);break;
 case 9:r=W::Enumerate(e,0,a[0],a+1,a[4]);break;
 case 10:r=W::Enumerate(e,1,a[0],a+1,a[7]);break;
 case 11:r=W::Enumerate(e,2,a[0],a+1,a[5]);break;
 case 12:r=W::GetValue(e,a[0],a[1],a[2],a[3],a[4],a[5]);break;
 case 13:r=W::SetValue(e,a[0],a[1],a[2],a[3],a[4],a[5]);break;
 case 14:r=W::DeleteValue(e,a[0],a[1],a[2]);break;
 case 15:r=W::DeleteEmpty(e,a[0],a[1]);break;
 case 16:r=W::Duplicate(e,a[0]);break;
 case 17:r=W::QueryExpanded(e,a[0],a[1],a[2],a[3],a[4],a[5]);break;
 case 18:r=e.RegEnumKey(a[0],a[1],a[2],a[3]);break;
 case 19:r=e.RegEnumValue(a[0],a[1],a[2],a[3],a[4],a[5],a[6]);break;
 case 20:r=e.RegInfo(a[0],a[1],a[2],a[3],a[4]);break;
 case 30:e.Write32(a[0],a[1]);break;
 default:assert(false);
 }
 assert(!e.scratch_used);out[0]=r;out[1]=e.last_error;
}
#ifdef SHELL_REGISTRY_STANDALONE
void Delta(FILE*f,unsigned char*p){U count;assert(fread(&count,4,1,f)==1);for(U i=0;i<count;++i){U offset,n;assert(fread(&offset,4,1,f)==1&&fread(&n,4,1,f)==1);assert(offset<=Size&&n<=Size-offset);assert(fread(p+offset,1,n,f)==n);}}
int main(int argc,char**argv){assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);U sequences,calls=0;assert(fread(&sequences,4,1,f)==1);
 for(U s=0;s<sequences;++s){U n;assert(fread(&n,4,1,f)==1);memset(memory,0,Size);Delta(f,memory);e=Environment{};std::vector<unsigned char>expected(memory,memory+Size);
  for(U i=0;i<n;++i){U a[16],want[2],got[2],trace_n;assert(fread(a,4,16,f)==16&&fread(want,4,2,f)==2);Delta(f,expected.data());assert(fread(&trace_n,4,1,f)==1);std::vector<U>trace(trace_n);assert(fread(trace.data(),4,trace_n,f)==trace_n);shell_registry_run(a,got);if(memcmp(got,want,sizeof got)||memcmp(memory,expected.data(),Size)||trace!=e.trace){fprintf(stderr,"shell registry mismatch %u/%u\n",s,i);return 1;}++calls;}}
 fclose(f);printf("SHELL REGISTRY ASAN/UBSAN PASS %u calls\n",calls);}
#endif
