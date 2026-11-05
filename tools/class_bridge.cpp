// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_classes.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cassert>
namespace {
using U=unsigned;constexpr U User=34*65536,Data=37*65536,Bytes=131072;
unsigned char memory[Bytes];U config[16],last_error=0,local_next=0x4000,body_next=0x6000,allocations=0;
U (*outside_read)(U)=nullptr;void(*outside_write)(U,U)=nullptr;
std::vector<U> events;U sizes[65536],body_sizes[65536];
void event(U op,U a=0,U b=0,U c=0){events.insert(events.end(),{op,a,b,c});}
struct E {
 static constexpr U Temporary=0x60000000;unsigned char scratch[4096]{};U used=0;
 struct Scratch{E&e;U address,bytes;Scratch(E&x,U n):e(x),address(Temporary+x.used),bytes((n+3)&~3u){assert(bytes+e.used<=4096);e.used+=bytes;}~Scratch(){e.used-=bytes;}};
 U Window(U p){return p?User+p:0;}
 U StringToken(U p){return p>=Temporary?p:Far(p);}

 U Local(U p){return User+(p&65535);} U LocalOffset(U p){return p-User;}
 U Read8(U p){if(p>=User&&p<User+65536)return memory[p-User];if(p>=Data&&p<Data+65536)return memory[65536+p-Data];if(p>=Temporary&&p<Temporary+4096)return scratch[p-Temporary];if(outside_read)return outside_read(p);fprintf(stderr,"bad Read8 %x\n",p);assert(false);return 0;}
 U Read16(U p){return Read8(p)|(Read8(p+1)<<8);}U Read32(U p){return Read16(p)|(Read16(p+2)<<16);}
 void Write8(U p,U v){if(p>=User&&p<User+65536)memory[p-User]=v;else if(p>=Data&&p<Data+65536)memory[65536+p-Data]=v;else if(p>=Temporary&&p<Temporary+4096)scratch[p-Temporary]=v;else if(outside_write)outside_write(p,v);else assert(false);}
 void Write16(U p,U v){Write8(p,v);Write8(p+1,v>>8);}void Write32(U p,U v){Write16(p,v);Write16(p+2,v>>16);}
 U ReadPointer(U p){return StringPointer(Read32(p));}
 U Far(U p){return ((p/65536)*8<<16)|(p&65535);}
 void WritePointer(U p,U v){Write32(p,Far(v));}void WriteStringPointer(U p,U v){WritePointer(p,v);}
 U StringPointer(U v){return v>=Temporary?v:(v>>19)*65536+(v&65535);}bool IsString(U v){return (v>>16)!=0;}
 U ModuleHandle(U v){event(1,v);return v;}
 U CurrentOwner(){event(2);return 0x1800;}
 U ExpectedVersion(U v){event(3,v);return config[0];}
 U CurrentExpectedVersion(){event(4);return config[1];}
 bool ModuleIsLibrary(U v){event(5,v);return config[2]&1;}
 bool AllowedGlobalProcedure(U p){event(6,p);return config[2]&2;}
 void DuplicateClassError(){event(7);last_error=0x42;}
 bool BadRead(U p,U n){event(8,p,n);return config[2]&4;}
 bool BadInstance(U p){event(9,p);return config[2]&8;}
 U StackInstance(){event(10);return 0x1000;}
 bool BadCode(U p){event(11,p);return config[2]&16;}
 bool BadString(U p,U n){event(12,p,n);return config[2]&32;}
 U ObjectType(U h){event(13,h);return config[2]&64?0:2;}
 bool ValidIcon(U h){event(14,h);return !(config[2]&128);}
 U ApplicationFlags(){event(15);return config[3];}
 U StringLength(U p){p=StringPointer(p);U n=0;while(Read8(p+n))++n;event(16,p,n);return n;}
 void StringCopy(U to,U from){from=StringPointer(from);event(17,to,from);U i=0;do{Write8(to+i,Read8(from+i));}while(Read8(from+i++));}
 U LocalAllocate(U flags,U bytes){const bool fail=++allocations==config[4];U p=fail?0:local_next;event(18,flags,bytes,p);if(p){assert(p+bytes<65536);for(U i=0;i<bytes;++i)Write8(Local(p+i),0);sizes[p]=bytes;local_next=(p+bytes+3)&~3u;}return p;}
 void LocalFree(U p){event(19,p);assert(sizes[p]);for(U i=0;i<sizes[p];++i)Write8(Local(p+i),0xdd);sizes[p]=0;}
 U BodyAllocate(U bytes){const bool fail=++allocations==config[4];U p=fail?0:Data+body_next;event(20,bytes,p);if(p){for(U i=0;i<bytes;++i)Write8(p+i,0);body_sizes[p&65535]=bytes;body_next=(body_next+bytes+3)&~3u;}return p;}
 void BodyFree(U p){event(21,p);for(U i=0;i<body_sizes[p&65535];++i)Write8(p+i,0xdd);body_sizes[p&65535]=0;}
 U AtomIndex(U value){if(!IsString(value))return value;return Read8(StringPointer(value))=='A'?0xc001:0xc002;}
 U FindAtom(U value){U atom=AtomIndex(value);if(atom>=0xc000&&!Read16(Local(0x1c00+(atom&255)*2)))atom=0;event(22,IsString(value)?0x10000|AtomIndex(value):value,atom);return atom;}
 U AddAtom(U value){U atom=AtomIndex(value);if(config[2]&256)atom=0;if(atom>=0xc000)Write16(Local(0x1c00+(atom&255)*2),Read16(Local(0x1c00+(atom&255)*2))+1);event(23,IsString(value)?0x10000|AtomIndex(value):value,atom);return atom;}
 void DeleteAtom(U atom){event(24,atom);if(atom>=0xc000)Write16(Local(0x1c00+(atom&255)*2),Read16(Local(0x1c00+(atom&255)*2))-1);}
 U AtomName(U atom,U output,U count){const char *text=atom==0xc001?"Alpha":"Beta";U n=strlen(text);if(n>=count)n=count?count-1:0;if(count){for(U i=0;i<n;++i)Write8(output+i,text[i]);Write8(output+n,0);}event(25,atom,count,n);return n;}
 void BrushReference(U h,bool add){event(26,h,add);}
 U ProcedureKind(U p){event(27,Read32(p));return config[5];}
 U ProcedureAlias(U p){event(28,p);return p^0x10000000;}
 void DestroyClassDC(U dc){event(29,dc);}
 void NotifyClassAtomRemoved(U atom){event(30,atom);}
 void ReleaseClassBrush(U owner,U record){Win98Classes::ReleaseBrush(*this,owner,record);}
 bool ReleaseDerivedIcon(U record){return Win98Classes::ReleaseSmallIcon(*this,record);}
 void ClearIconCache(U icon){Win98Classes::ClearSmallIconCache(*this,icon);}
 void WarnSystemDialogClass(){event(34);}
 U CloneSystemClass(U window,U record){return Win98Classes::Clone(*this,window,record);}
 U SetClassIcon(U window,int index,U value){return Win98Classes::SetIcon(*this,window,index,value);}
 void SetClassProcedure(U window,U value){Win98Classes::SetProcedure(*this,window,value);}
 U WindowOwner(U window){event(38,window);return 0x1800;}
 void SetClassBrush(U owner,U record,U value){Win98Classes::SetBrush(*this,owner,record,value);}
 void MemoryCopy(U to,U from,U n){event(40,to,from,n);Win98Classes::Copy(*this,to,from,n);}
 void DestroyDerivedIcon(U icon,U flags){event(41,icon,flags);}
 U CopySmallIcon(U module,U source,U kind,U width,U height,U flags){assert(kind==1&&flags==0x4000);event(42,source,width,height);return config[8];}
 void RefreshClassIcons(U window,U record,bool all){event(43,window,record,all);}
 void DeleteBrush(U h){event(44,h);}

};
}
extern "C" void class_callbacks(U(*read)(U),void(*write)(U,U)){outside_read=read;outside_write=write;}
extern "C" void class_reset(const unsigned char *m){memcpy(memory,m,Bytes);memset(sizes,0,sizeof sizes);memset(body_sizes,0,sizeof body_sizes);local_next=0x4000;body_next=0x6000;allocations=0;events.clear();}
extern "C" const unsigned char *class_memory(){return memory;}
extern "C" const U *class_events(U*n){*n=events.size();return events.data();}
extern "C" void class_begin(const U *c){memcpy(config,c,16*4);events.clear();last_error=c[15];allocations=0;}
extern "C" U class_provider(U op,const U*a){E e;switch(op){
case 1:return e.ModuleHandle(a[0]);case 2:return e.CurrentOwner();case 3:return e.ExpectedVersion(a[0]);case 4:return e.CurrentExpectedVersion();case 5:return e.ModuleIsLibrary(a[0]);case 6:return e.AllowedGlobalProcedure(a[0]);case 7:e.DuplicateClassError();return 0;
case 8:return e.BadRead(a[0],a[1]);case 9:return e.BadInstance(a[0]);case 10:return e.StackInstance();case 11:return e.BadCode(a[0]);case 12:return e.BadString(a[0],a[1]);case 13:return e.ObjectType(a[0]);case 14:return e.ValidIcon(a[0]);case 15:return e.ApplicationFlags();
case 16:return e.StringLength(a[0]);case 17:e.StringCopy(a[0],a[1]);return 0;case 18:return e.LocalAllocate(a[0],a[1]);case 19:e.LocalFree(a[0]);return 0;case 20:return e.BodyAllocate(a[0]);case 21:e.BodyFree(a[0]);return 0;
case 22:return e.FindAtom(a[0]);case 23:return e.AddAtom(a[0]);case 24:e.DeleteAtom(a[0]);return 0;case 25:return e.AtomName(a[0],a[1],a[2]);case 26:e.BrushReference(a[0],a[1]);return 0;case 27:return e.ProcedureKind(a[0]);case 28:return e.ProcedureAlias(a[0]);
case 29:e.DestroyClassDC(a[0]);return 0;case 30:e.NotifyClassAtomRemoved(a[0]);return 0;case 31:e.ReleaseClassBrush(a[0],a[1]);return 0;case 32:return e.ReleaseDerivedIcon(a[0]);case 33:e.ClearIconCache(a[0]);return 0;case 34:e.WarnSystemDialogClass();return 0;case 35:return e.CloneSystemClass(a[0],a[1]);case 36:return e.SetClassIcon(a[0],int(a[1]),a[2]);case 37:e.SetClassProcedure(a[0],a[1]);return 0;case 38:return e.WindowOwner(a[0]);case 39:e.SetClassBrush(a[0],a[1],a[2]);return 0;case 40:e.MemoryCopy(a[0],a[1],a[2]);return 0;case 41:e.DestroyDerivedIcon(a[0],a[1]);return 0;case 42:return e.CopySmallIcon(a[0],a[1],a[2],a[3],a[4],a[5]);case 43:e.RefreshClassIcons(a[0],a[1],a[2]);return 0;case 44:e.DeleteBrush(a[0]);return 0;case 50:return last_error;
}assert(false);return 0;}
extern "C" void class_run(const U *a,const U*c,U*out){E e;class_begin(c);U r=0;using namespace Win98Classes;switch(a[0]){
case 0:r=Validate(e,a[1]);break;case 1:r=Register(e,a[1]);break;case 2:r=Find(e,a[1],a[2],a[3],a[4]);break;case 3:r=GetInfo(e,a[1],a[2],a[3]);break;case 4:r=Unregister(e,a[1],a[2]);break;case 5:r=Access(e,a[1],a[2],a[3],a[4]);break;case 6:r=GetName(e,a[1],a[2],a[3]);break;
case 10:e.Write16(a[1],a[2]);break;case 11:e.Write32(a[1],a[2]);break;default:assert(false);
}out[0]=r;out[1]=last_error;}
#ifdef CLASS_STANDALONE
static void read_exact(FILE*f,void*p,size_t n){assert(fread(p,1,n,f)==n);}
static U read_u(FILE*f){U n;read_exact(f,&n,4);return n;}
static void delta(FILE*f,std::vector<unsigned char>&m){for(U n=read_u(f);n;--n){U p=read_u(f),bytes=read_u(f);assert(p<=Bytes&&bytes<=Bytes-p);read_exact(f,m.data()+p,bytes);}}
int main(int argc,char**argv){assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);U calls=0;for(U n=read_u(f);n;--n){U steps=read_u(f);std::vector<unsigned char>m(Bytes);delta(f,m);class_reset(m.data());while(steps--){U a[6],c[16],want[2],got[2];read_exact(f,a,sizeof a);read_exact(f,c,sizeof c);read_exact(f,want,sizeof want);delta(f,m);U count=read_u(f);std::vector<U>trace(count);read_exact(f,trace.data(),count*4);class_run(a,c,got);assert(!memcmp(got,want,8));assert(!memcmp(m.data(),class_memory(),Bytes));U actual;const U*p=class_events(&actual);assert(count==actual&&!memcmp(trace.data(),p,count*4));++calls;}}fclose(f);printf("CLASS ASAN/UBSAN PASS %u calls\n",calls);}
#endif
