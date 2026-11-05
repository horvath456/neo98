// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_process_legacy.h"
#include <cassert>
#include <cstring>
using U=unsigned;static unsigned char arena[65536],scratch[4096];static U events[8192],count,top,allocations;static U(*reader)(U);static void(*writer)(U,U);static Win98PathFamily::State mode;
struct E{
 struct Scratch{U address,old;Scratch(E&,U n):old(top){address=0x20000000+top;top+=n;assert(top<sizeof scratch);}~Scratch(){top=old;}};
 U Read8(U p){if(p>=0x100000&&p<0x110000)return arena[p-0x100000];if(p>=0x20000000&&p<0x20001000)return scratch[p-0x20000000];assert(reader);return reader(p);}
 U Read16(U p){return Read8(p)|(Read8(p+1)<<8);}U Read32(U p){return Read16(p)|(Read16(p+2)<<16);}
 void Write8(U p,U v){if(p>=0x100000&&p<0x110000)arena[p-0x100000]=v;else if(p>=0x20000000&&p<0x20001000)scratch[p-0x20000000]=v;else{assert(writer);writer(p,v);}}
 void Write16(U p,U v){Write8(p,v);Write8(p+1,v>>8);}void Write32(U p,U v){Write16(p,v);Write16(p+2,v>>16);}
 void Event(U a,U b=0,U c=0,U d=0){assert(count+4<=8192);events[count++]=a;events[count++]=b;events[count++]=c;events[count++]=d;}
 void Text(U p,const char*s){do{Write8(p++,*s);}while(*s++);}
 U Hash(U p){if(!p)return 0;U h=1;for(U c;(c=Read8(p++));)h=h*33+c;return h;}
 U LastError(){Event(1);return Read32(0x10f000);}
 void LockStartup(){Event(2);}void UnlockStartup(){Event(3);}U Startup(){return 0x107000;}
 void Copy(U to,U from,U n){Event(4,to,from,n);unsigned char data[1024];assert(n<=sizeof data);for(U i=0;i<n;++i)data[i]=Read8(from+i);for(U i=0;i<n;++i)Write8(to+i,data[i]);}
 U ConvertInput(U p,U d){Event(5,p,d);for(U i=0;;++i){U c=Read8(p+i);Write8(0x10c000+i,c);if(!c)break;}return 0x10c000;}
 void FreeInput(U p,U d){Event(6,p,d);}
 U GetDirectory(U n,U out){Event(7,n);Text(out,"C:\\TEMP");return 7;}
 U SetDirectory(U p){Event(8,Hash(p));return 1;}
 U Create(const U*a){Event(9,Hash(a[0]),Hash(a[1]),a[6]);Event(10,Hash(a[7]),a[2],a[3]);Event(11,a[4],a[5]);for(U i=0;i<68;i+=4)Event(12,Read32(a[8]+i));if(!Read32(0x10f004))return 0;Write32(a[9],0x601);Write32(a[9]+4,0x602);Write32(a[9]+8,3);Write32(a[9]+12,4);return 1;}
 U Allocate(U n){Event(13,n);if(++allocations==Read32(0x10f00c))return 0;return 0x107000+allocations*4096;}
 void Free(U p){Event(14,p);}
 void Close(U h){Event(15,h);}
 U Open(U p){Event(16,Hash(p));return Read32(0x10f02c);}
 U Search(U name,U out,U cap){Event(17,name,out,cap);U op=Read32(0x10f008);if(!op)return 0;if(op==2)return 261;Text(out,"C:\\WINDOWS\\LEGACY.EXE");return 21;}
 U ThreadTask(U tid){Event(18,tid);return Read16(0x104014);}
 U ReferenceProcess(U h,U access,U flag){Event(19,h,access,flag);return Read32(0x10f010)?0x105000:0;}
 U ProcessFlags(U p){return Read32(p+0x20);}void DereferenceProcess(U p){Event(20,p);}
 U WaitThread(U tid){ThreadTask(tid);Event(21,0x104000);return Read32(0x10f028);}
 U SelectorExists(U task){Event(22,task);return Read32(0x10f01c);}
 U SelectorAddress(U value){Event(23,value);return 0x106000;}
 U Finish(U k,U created,U pi){Write8(0x103045,mode.thread_flags);return Win98ProcessLegacy::Finish(*this,k,created,pi);}
};
extern "C" void legacy_reset(const unsigned char*p){memcpy(arena,p,sizeof arena);memset(scratch,0,sizeof scratch);count=top=allocations=0;mode={};E e;mode.process_flags=e.Read32(0x10f030);mode.thread_flags=e.Read8(0x103045);}
extern "C" unsigned char*legacy_memory(){return arena;}
extern "C" U legacy_events(U*out){memcpy(out,events,count*4);return count;}
extern "C" void legacy_callbacks(U(*r)(U),void(*w)(U,U)){reader=r;writer=w;}
extern "C" U legacy_run(U op,const U*a){E e;using namespace Win98ProcessLegacy;switch(op){case 0:return WinExec(e,mode,a[0],a[1]);case 1:return LoadModule(e,a[0],a[1]);case 2:return Finish(e,a[0],a[1],a[2]);case 3:Startup(e,a[0]);return 0;}assert(false);return 0;}
extern "C" U legacy_provider(U op,const U*a){E e;switch(op){case 1:return e.LastError();case 2:e.LockStartup();return 0;case 3:e.UnlockStartup();return 0;case 4:e.Copy(a[0],a[1],a[2]);return a[0];case 5:return e.ConvertInput(a[0],a[1]);case 6:e.FreeInput(a[0],a[1]);return 0;case 7:return e.GetDirectory(a[0],a[1]);case 8:return e.SetDirectory(a[0]);case 9:return e.Create(a);case 13:return e.Allocate(a[0]);case 14:e.Free(a[0]);return 0;case 15:e.Close(a[0]);return 1;case 16:assert(a[1]==0x80000000&&a[2]==3&&a[3]==0&&a[4]==3&&a[5]==0x80&&a[6]==0);return e.Open(a[0]);case 17:assert(a[0]==0&&a[2]==0xbffc9c18&&a[5]==0);return e.Search(a[1],a[4],a[3]);case 18:e.ThreadTask(a[0]);return 0x104000;case 19:return e.ReferenceProcess(a[0],a[1],a[2]);case 20:e.DereferenceProcess(a[0]);return 0;case 21:e.Event(21,a[0]);return e.Read32(0x10f028);case 22:return e.SelectorExists(a[0]);case 23:return e.SelectorAddress(a[0]);case 24:mode.process_flags=e.Read32(0x10f030);mode.thread_flags=e.Read8(0x103045);{U ret=Win98PathFamily::FileApiMode(mode,a[0]);e.Write8(0x103045,mode.thread_flags);return ret;}case 25:for(U i=0;i<a[1];++i)e.Write8(a[0]+i,a[2]);return a[0];case 26:return Win98ProcessLegacy::Length(e,a[0]);case 27:Win98ProcessLegacy::Append(e,a[0],a[1]);return a[0];}assert(false);return 0;}
