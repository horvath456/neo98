// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_window_parent.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace Win98WindowParent;
struct Replay:Environment {
 Node nodes[16]{};int clients[16][2]{};unsigned action=0,trace[32][5]{},count=0;bool valid=true,first=true;
 Node Read(unsigned h)const override{return h<16?nodes[h]:Node{};}
 void Write(unsigned h,const Node&w)override{assert(h<16);nodes[h]=w;}
 unsigned Service(unsigned op,unsigned h,unsigned a,unsigned b,unsigned c)override {
  assert(count<32);unsigned*row=trace[count++];row[0]=op;row[1]=h;row[2]=a;row[3]=b;row[4]=c;
  if(op==Show){auto&w=nodes[h];const unsigned old=w.style&0x10000000?0x10:0;w.style=(w.style&~0x10000000u)|(a?0x10000000:0);
   if(first){first=false;if(action==1)valid=false;if(action==2)w.parent=5;if(action==3)w.flags|=0x10;if(action==4)nodes[4].flags|=0x10;}return old;}
  if(op==Valid)return valid;
  if(op==Map){unsigned x=h==1?0:clients[h][0],y=h==1?0:clients[h][1];return ((a-x)&65535)|((b-y)<<16);}
  if(op==Place&&action==5){valid=false;nodes[h].flags|=0x10;}
  if(op==Place&&action==6)nodes[h].parent=5;
  if(op==Refresh&&action==7)nodes[h].extended^=8;
  if(op==Link&&action==8){nodes[h].task=2;nodes[h].style^=0x40000000;}
  if(op==Front)return 6;
  return 0;
 }
};
static void read(FILE*f,void*p,size_t size){assert(fread(p,1,size,f)==size);}
int main(int argc,char**argv){assert(argc==3);FILE*f=fopen(argv[1],"rb");assert(f);unsigned cases;read(f,&cases,4);unsigned total=0;
 for(unsigned i=0;i<cases;++i){unsigned args[4],count,trace[32][5];Node expected[16];Replay e;e.desktop=1;e.special=2;read(f,args,sizeof args);e.action=args[1];e.accessibility=args[2];read(f,e.nodes,sizeof e.nodes);read(f,e.clients,sizeof e.clients);read(f,expected,sizeof expected);read(f,&count,4);assert(count<=32);read(f,trace,count*20);assert(SetParent(e,3,args[0])==args[3]);assert(!memcmp(e.nodes,expected,sizeof expected));assert(e.count==count&&!memcmp(e.trace,trace,count*20));total+=count;}
 assert(fgetc(f)==EOF);fclose(f);
 f=fopen(argv[2],"rb");assert(f);unsigned row[5],decisions=0;
 while(fread(row,sizeof row,1,f)==1){unsigned result;Position p=Raise(row[1]);switch(row[0]){
 case 0:assert(p.hwnd==row[1]&&!p.after&&!p.x&&!p.y&&!p.width&&!p.height);result=p.flags;break;
 case 1:p.flags=row[2];result=ActivationCandidate(p);break;
 case 2:p.flags=row[1];result=Changing(p);break;
 case 3:p.flags=row[1];result=Changed(p);break;
 default:result=ActivationKind(row[1],row[2],row[3],true);break;
 }assert(result==row[4]);++decisions;}
 assert(feof(f));fclose(f);printf("Window parent decision sanitizer replay PASS cases=%u\n",decisions);
 printf("Window parent sanitizer replay PASS cases=%u provider_calls=%u\n",cases,total);
}
