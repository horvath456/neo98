// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_classes.h"
#include <new>
#include "win98_atoms.h"
#include "win98_local_heap.h"
#ifdef NEO98_RUNTIME_CONTENT
#include "windows_runtime_data.h"
#else
#include "../build/generated/atom_upper.h"
#endif
#if defined(__APPLE__) || defined(__linux__)
#include <string.h>
#else
#include <circle/util.h>
#endif
namespace Win98ClassStore {
using U=unsigned;
// Architecture provider: original WORD class links address this private arena.
// Guest pointers never alias these bytes. Module identities are mapped tokens.
struct Store {
 unsigned char memory[196608]{}; U window_owners[1024]{};
 struct ModuleEntry {U value=0,owner=0;};
 struct Body {bool used=false;unsigned char bytes[32]{};};
 ModuleEntry *modules=nullptr;U module_capacity=0;
 U *handles=nullptr,handle_capacity=0;
 Body *bodies=nullptr;U body_capacity=0;
 // Tokens in WORD fields stay below FFFE/FFFF, which USER reserves.
 // Class bodies use DWORD pointers and live beyond the private USER arena.
 template<class T> bool Grow(T *&table,U &capacity,U limit){
  if(capacity==limit){error=8;return false;}
  U n=capacity?capacity*2:64;if(n>limit)n=limit;
  auto *more=new(std::nothrow) T[n]{};if(!more){error=8;return false;}
  for(U i=0;i<capacity;++i)more[i]=table[i];
  delete[] table;table=more;capacity=n;return true;
 }
 ~Store(){delete[] modules;delete[] handles;delete[] bodies;}
 Store(const Store&)=delete;Store&operator=(const Store&)=delete;
 U current_owner=0x1800; U expected=0x400,error=0; U (*object_type)(U)=nullptr; void (*delete_brush)(U)=nullptr; bool system_registration=false;
 struct Heap:Win98LocalHeap::Environment {
  explicit Heap(unsigned char*p):Environment(p){} U Compact(U,U&b)override{b=0;return 0;}
  U Notify(U,U,U)override{return 0;} void Error(U)override{}
 } heap;
 struct Atoms:Win98Atoms::Environment {
  unsigned char memory[65536]{};Heap heap;
  Atoms():Environment(memory,OriginalAtomUpper),heap(memory){Reset();}
  void Reset(){memset(memory,0,sizeof memory);Win98LocalHeap::Initialize(heap,0x1000,0xfff0);}
  U Allocate(U flags,U size)override{return Win98LocalHeap::Allocate(heap,flags,size);}
  void Free(U p)override{Win98LocalHeap::Free(heap,p);}
  bool Valid(U p)const override {
   if(p<0x1004||p>=0xfff0||(p&3))return false;
   U control=Word(6),tail=Word(control+10),block=Word(Word(control+6)+2);
   for(U n=0;block<tail&&n<16384;++n){U next=Word(block+2);if(next<=block||next>tail)return false;if(block+4==p)return (Word(block)&3)==1&&next-block>=12&&Byte(p+4)&&Byte(p+4)+10<=next-block;block=next;}return false;
  }
 } atoms;
 Store():heap(memory){Reset();}
 void Reset(){memset(memory,0,sizeof memory);delete[] modules;modules=nullptr;module_capacity=0;memset(window_owners,0,sizeof window_owners);current_owner=0x1800;delete[] handles;handles=nullptr;handle_capacity=0;delete[] bodies;bodies=nullptr;body_capacity=0;atoms.Reset();Win98LocalHeap::Initialize(heap,0x5000,0xfff0);Write16(0xe28,0x1800);Write16(0xae,0x100);Write16(0xb0,0xfffe);Write16(0x340,16);Write16(0x342,16);error=0;}
 U Local(U p){return p&65535;} U LocalOffset(U p){return p&65535;}
 U Read8(U p){if(p<sizeof memory)return memory[p];p-=sizeof memory;return p/32<body_capacity&&bodies[p/32].used?bodies[p/32].bytes[p%32]:0;} U Read16(U p){return Read8(p)|(Read8(p+1)<<8);} U Read32(U p){return Read16(p)|(Read16(p+2)<<16);}
 void Write8(U p,U v){if(p<sizeof memory)memory[p]=v;else{p-=sizeof memory;if(p/32<body_capacity&&bodies[p/32].used)bodies[p/32].bytes[p%32]=v;}}void Write16(U p,U v){Write8(p,v);Write8(p+1,v>>8);}void Write32(U p,U v){Write16(p,v);Write16(p+2,v>>16);}
 U ReadPointer(U p){return Read32(p);}void WritePointer(U p,U v){Write32(p,v);}void WriteStringPointer(U p,U v){Write32(p,v|0x80000000);}
 U StringPointer(U p){return p&0x7fffffff;} U StringToken(U p){return p|0x80000000;}bool IsString(U p){return p>65535;}
 struct Scratch {Store&e;U address;Scratch(Store&s,U):e(s),address(0x1f000){} };
 U Module(U value,bool create=true){
  if(value==0xffffffff)return 0xfffe;if(!value)return 0;
  U free_slot=module_capacity;
  for(U i=0;i<module_capacity;++i){
   if(modules[i].value==value&&modules[i].owner==CurrentOwner())return 0x100+i;
   if(!modules[i].value&&free_slot==module_capacity)free_slot=i;
  }
  if(!create)return 0xffff;
  if(free_slot==module_capacity&&!Grow(modules,module_capacity,0xfffe - 0x100))return 0;
  modules[free_slot]={value,CurrentOwner()};return 0x100+free_slot;
 }
 void ReleaseModules(U owner){for(U i=0;i<module_capacity;++i)if(modules[i].owner==owner)modules[i]={};}
 U ModuleValue(U value){return value>=0x100&&value-0x100<module_capacity?modules[value-0x100].value:0;}
 U Handle(U value){
  if(value<=29)return value;U free_slot=handle_capacity;
  for(U i=0;i<handle_capacity;++i){if(handles[i]==value)return 0x1000+i;if(!handles[i]&&free_slot==handle_capacity)free_slot=i;}
  if(free_slot==handle_capacity&&!Grow(handles,handle_capacity,0xffff-0x1000))return 0;
  handles[free_slot]=value;return 0x1000+free_slot;
 }
 U HandleValue(U value){return value>=0x1000&&value-0x1000<handle_capacity?handles[value-0x1000]:value;}
 U ModuleHandle(U v){return v;}U CurrentOwner(){return current_owner;}U ExpectedVersion(U){return expected;}U CurrentExpectedVersion(){return expected;}
 bool ModuleIsLibrary(U){return true;}bool AllowedGlobalProcedure(U){return true;}void DuplicateClassError(){error=0x42;}
 bool BadRead(U p,U n){return p>=sizeof memory||n>sizeof memory-p;}bool BadInstance(U p){return !p;}U StackInstance(){return 0x100;}bool BadCode(U p){return !p;}
 bool BadString(U p,U n){p=StringPointer(p);for(U i=0;i<n;++i){if(p+i>=sizeof memory)return true;if(!Read8(p+i))return false;}return true;}
 U ObjectType(U h){return object_type?object_type(HandleValue(h)):0;}bool ValidIcon(U h){return h!=0xffff;}U ApplicationFlags(){return 0;}
 U StringLength(U p){p=StringPointer(p);U n=0;while(p+n<sizeof memory&&Read8(p+n))++n;return n;}
 void StringCopy(U to,U from){from=StringPointer(from);for(U i=0;i<256;++i){U c=Read8(from+i);Write8(to+i,c);if(!c)break;}}
 U LocalAllocate(U flags,U bytes){U p=Win98LocalHeap::Allocate(heap,flags,bytes);if(!p)error=8;return p;}void LocalFree(U p){Win98LocalHeap::Free(heap,p);}
 U BodyAllocate(U){
  U i=0;while(i<body_capacity&&bodies[i].used)++i;
  if(i==body_capacity&&!Grow(bodies,body_capacity,(0x7fffffffu-sizeof memory)/32))return 0;
  bodies[i]={};bodies[i].used=true;return sizeof memory+i*32;
 }
 void BodyFree(U p){if(p>=sizeof memory&&(p-sizeof memory)/32<body_capacity)bodies[(p-sizeof memory)/32]={};}
 U FindAtom(U name){return IsString(name)?Win98Atoms::Search(atoms,memory+StringPointer(name),0):Win98Atoms::Integer(name);}
 U AddAtom(U name){return IsString(name)?Win98Atoms::Search(atoms,memory+StringPointer(name),1):Win98Atoms::Integer(name);}
 void DeleteAtom(U atom){Win98Atoms::Delete(atoms,atom);}U AtomName(U atom,U out,U capacity){if(out>=sizeof memory)return 0;if(capacity>sizeof memory-out)capacity=sizeof memory-out;return Win98Atoms::Name(atoms,atom,memory+out,capacity);}
 void BrushReference(U,bool){}U ProcedureKind(U){return system_registration?8:2;}U ProcedureAlias(U p){return p;}
 void DestroyClassDC(U){}void NotifyClassAtomRemoved(U){}void DeleteBrush(U h){if(delete_brush)delete_brush(HandleValue(h));}
 void ReleaseClassBrush(U owner,U record){Win98Classes::ReleaseBrush(*this,owner,record);}bool ReleaseDerivedIcon(U r){return Win98Classes::ReleaseSmallIcon(*this,r);}void ClearIconCache(U i){Win98Classes::ClearSmallIconCache(*this,i);}
 void WarnSystemDialogClass(){}U Window(U p){return p?0x20000+p:0;}U WindowOwner(U w){U n=w>=0x20000?(w-0x20000)/64:1024;return n<1024&&window_owners[n]?window_owners[n]:CurrentOwner();}
 U CloneSystemClass(U w,U r){return Win98Classes::Clone(*this,w,r);}U SetClassIcon(U w,int i,U v){return Win98Classes::SetIcon(*this,w,i,v);}void SetClassProcedure(U w,U v){Win98Classes::SetProcedure(*this,w,v);}
 void SetClassBrush(U o,U r,U v){Win98Classes::SetBrush(*this,o,r,v);}void MemoryCopy(U to,U from,U n){Win98Classes::Copy(*this,to,from,n);}
 void DestroyDerivedIcon(U,U){}U CopySmallIcon(U,U,U,U,U,U){return 0;}void RefreshClassIcons(U,U,bool){}
 // USER hands the windows of a class on before it goes; not
 // implemented, so the walk of DestroyModule stops and the class stays.
 bool ClassWindowsRemain(U){return false;}
 U NameToken(const char*name,U slot=0x10000){if(!name)return 0;U i=0;for(;i<255&&name[i];++i)Write8(slot+i,(unsigned char)name[i]);Write8(slot+i,0);return slot;}
 U FindRecord(U instance,const char*name){U link=Win98Classes::Find(*this,CurrentOwner(),NameToken(name),Module(instance,false),7);return link?Read16(link):0;}
 U Register(const U*f,const char*name,const char*menu){
  error=0;if(!name||strlen(name)>=256||(menu&&strlen(menu)>=256))return 0;U p=0x11000;Write32(p,f[0]-14);Write32(p+4,f[1]);Write32(p+8,f[2]);Write16(p+12,f[3]);Write16(p+14,f[4]);Write16(p+16,Module(f[5]));
  Write16(p+18,Handle(f[6]));Write16(p+20,Handle(f[7]));Write16(p+22,Handle(f[8]));Write32(p+24,menu?NameToken(menu,0x10400):f[9]);Write32(p+28,NameToken(name));Write16(p+32,Handle(f[11]));
  if(error)return 0;
  if(!system_registration&&!Win98Classes::Validate(*this,p))return 0;if(!system_registration&&CurrentOwner()!=0x1800)Write32(p+4,Read32(p+4)|0x8000);return Win98Classes::Register(*this,p);
 }
 U Info(U instance,const char*name,U*f){U p=0x11000;Write32(p,34);U result=Win98Classes::GetInfo(*this,Module(instance,false),NameToken(name),p);if(!result)return 0;
  f[1]=Read32(p+4);f[2]=Read32(p+8);f[3]=Read16(p+12);f[4]=Read16(p+14);f[5]=instance;f[6]=HandleValue(Read16(p+18));f[7]=HandleValue(Read16(p+20));f[8]=HandleValue(Read16(p+22));f[9]=Read32(p+24);f[10]=0;f[11]=HandleValue(Read16(p+32));return result;
 }
};
}
