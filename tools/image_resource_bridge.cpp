// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_image_resource.h"
#include <string.h>
using namespace Win98ImageResource;
using U=unsigned;using Service=U(*)(U,U,U,U,U,U);
struct Wire {Word display[6];U heads[3];Record records[32];unsigned char directory[256],image[256];Word version;};
struct E:Environment {
 Wire&w;Service fn;E(Wire&x,Service f):w(x),fn(f){}
 U event(U op,U a=0,U b=0,U c=0,U d=0,U f=0){return fn(op,a,b,c,d,f);}
 Win98ImageDirectory::Display Display()override{return {w.display[0],w.display[1],w.display[2],w.display[3],w.display[4],w.display[5]};}
 U SystemInstance()override{return 0x150;}U SystemModule()override{return 0x158;}U CompatibilityInstance()override{return 0x168;}
 Word ExpectedVersion()override{return w.version;}U ApplicationTable()override{return 0x2106;}U TaskTable()override{return 0x2006;}U SystemTable()override{return 0xfc;}
 bool Win32Module(U h)override{return h==0x160;}U ModuleIdentity(U h)override{return event(21,h);}
 unsigned ti(U t){return t==0xfc?0:t==0x2006?1:2;}U Head(U t)override{return w.heads[ti(t)];}void Head(U t,U h)override{w.heads[ti(t)]=h;}
 Record&Item(U h)override{return w.records[(h-0x3000)/0x20];}
 U AllocateRecord()override{return event(11);}void ReleaseRecord(U h)override{event(12,h);}
 Word FindAtom(Name n)override{return event(13,n.value);}Word AddAtom(Name n)override{return event(14,n.value);}void DeleteAtom(Word a)override{event(15,a);}
 U FindResource(U m,Name n,Word t)override{return event(1,m,n.value,t);}U LoadResource(U m,U r)override{return event(2,m,r);}
 Block LockResource(U r)override{if(!event(3,r))return {};return {r==0x540?w.directory:w.image,256};}
 void UnlockResource(U r)override{event(4,r);}bool FreeResource(U r)override{return event(5,r)!=0;}
 U GlobalHandle(U r)override{return event(6,r);}Word GlobalFlags(U r)override{return event(7,r);}
 U CreateBitmap(Block,Word x,Word y,Word f)override{return event(8,x,y,f);}
 U CreateIcon(Block,bool icon,U owner,Word x,Word y,Word f)override{return event(9,owner,icon,x,y,f);}
 U LoadFile(Name n,Word t,Word x,Word y,Word f)override{return event(10,n.value,t,x,y,f);}
 U DrawSystemBitmap(Word r,Word x,Word y,Word f)override{return event(22,r,x,y,f);}
 void BeforeDestroy(U h)override{event(17,h);}U FreeImage(U h)override{return event(16,h);}
 void FixSystemImage(U h)override{event(18,h);}void PublishSystemImage(bool icon,U i,U h)override{event(19,icon,i,h);}
 void CustomizeSystemImages(bool icon)override{event(20,icon);}
};
extern "C" U image_resource_call(Wire*w,Service f,U op,U a,U b,U c,U d,U x,U y){E e(*w,f);switch(op){
case 0:return Load(e,a,{b,b>65535},c,d,x,y);
case 1:return SharedFind(e,a,b,{c,c>65535},d!=0);
case 2:return SharedAdd(e,a,b,{c,c>65535},d,x!=0);
case 3:return SharedRecord(e,a,b);
case 4:SharedRemove(e,a,b);return 0;
case 5:return Destroy(e,a,b);
case 6:InitializeSystemImages(e,a!=0);return 0;
case 7:return Create(e,{w->image,256},a,b,c,d,x);
}return 0;}
