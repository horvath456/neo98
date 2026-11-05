// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_print_dialog.h"
#include <string.h>
#include <assert.h>
struct Environment {
 static constexpr unsigned D=0x100000;unsigned char memory[65536]={};const unsigned*o;
 unsigned error=777,last=99,heap=D+0x800,event_count=0,events[2048]={};bool alive=true;
 explicit Environment(const unsigned*options):o(options){
  Write(D+0x100,o[1]);Write(D+0x104,o[2]);Write(D+0x108,o[3]);Write(D+0x10c,o[4]);Write(D+0x110,0x12345678);Write(D+0x114,o[5]);
  strcpy((char*)memory+0x200,"Fixture");Word(D+0x302,8);strcpy((char*)memory+0x308,"Fixture");strcpy((char*)memory+0x4500,"winspool");strcpy((char*)memory+0x4520,"FILE:");
 }
 void Event(unsigned n,unsigned a=0,unsigned b=0){assert(event_count+3<=2048);events[event_count++]=n;events[event_count++]=a;events[event_count++]=b;}
 unsigned Read(unsigned p){unsigned v;assert(p>=D&&p<=D+65532);memcpy(&v,memory+(p-D),4);return v;}
 void Write(unsigned p,unsigned v){assert(p>=D&&p<=D+65532);memcpy(memory+(p-D),&v,4);}
 unsigned Byte(unsigned p){assert(p>=D&&p<D+65536);return memory[p-D];}void Byte(unsigned p,unsigned v){assert(p>=D&&p<D+65536);memory[p-D]=v;}
 unsigned Word(unsigned p){return Byte(p)|(Byte(p+1)<<8);}void Word(unsigned p,unsigned v){Byte(p,v);Byte(p+1,v>>8);}
 bool Alive(){return alive;}void ErrorLock(){Event(30);}void ErrorUnlock(){Event(31);}
 void ErrorWrite(unsigned v){error=v;Event(1,v);}unsigned ErrorRead(){last=0;Event(2,error);return error;}
 unsigned LastError(){Event(15,last);return last;}void LastError(unsigned v){last=v;Event(16,v);}
 bool IsWindow(unsigned h){Event(3,h);return h==0x123;}unsigned ProcessVersion(){Event(4);return 0x40000;}
 unsigned Buffer(){return D+0x4000;}unsigned DriverName(){return D+0x4500;}unsigned FilePort(){return D+0x4520;}unsigned DefaultName(){return D+0x4540;}
 unsigned Length(unsigned p){return strlen((char*)memory+(p-D));}void CopyString(unsigned d,unsigned s){strcpy((char*)memory+(d-D),(char*)memory+(s-D));}
 bool SameName(unsigned a,unsigned b){return !strcmp((char*)memory+(a-D),(char*)memory+(b-D));}
 unsigned Profile(const char*section,const char*,unsigned p,unsigned cap){
  bool defaults=!strcmp(section,"Windows");const char*value=defaults?(o[11]==1?"Fixture,old-driver,LPT1:":o[11]==2?"   Fixture,driver,   LPT1:":""):(o[12]?"driver,LPT1:":"");
  unsigned n=strlen(value);if(n>=cap)n=cap-1;memcpy(memory+(p-D),value,n);Byte(p+n,0);Event(7,defaults?1:2,cap);return n;
 }
 bool DeviceExists(unsigned name){return Profile("devices",(char*)memory+(name-D),D+0x4200,2)&&Byte(D+0x4200);}
 bool Services(){Event(8);return o[6];}
 unsigned Allocate(unsigned size,unsigned flags){unsigned h=o[9]?0:heap;if(h)heap+=(size+15)&~15;Event(9,size,flags);return h;}
 unsigned Reallocate(unsigned h,unsigned size,unsigned flags){Event(10,size,flags);return o[9]?0:h;}
 unsigned Lock(unsigned h){Event(5,h);return h==o[10]||h<D||h>=D+65536?0:h;}
 void Unlock(unsigned h){Event(6,h);}void Free(unsigned h){Event(11,h);}
 unsigned DocumentProperties(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned mode){Event(12,mode);return mode?o[8]:o[7];}
 void DriverError(unsigned,unsigned){Event(17);}
 unsigned CreateDC(bool information,unsigned,unsigned,unsigned,unsigned){Event(13,information);return information?o[13]:o[14];}
 void ClosePrinter(unsigned h){Event(14,h);}
 void Warning(unsigned,unsigned error){Event(98,error);}
 unsigned Interactive(unsigned,Win98PrintDialog::State&){Event(99);alive=false;return 0;}
};
extern "C" void print_run(const unsigned*options,unsigned*out,unsigned char*memory,unsigned*events){
 Environment e(options);out[0]=Win98PrintDialog::Run(e,options[0]?Environment::D+0x100:0);out[1]=e.error;out[2]=e.last;out[3]=e.heap;out[4]=e.event_count;memcpy(memory,e.memory,65536);memcpy(events,e.events,e.event_count*4);
}
extern "C" unsigned print_wide(unsigned pointer,unsigned*last){unsigned options[15]={};Environment e(options);unsigned result=Win98PrintDialog::Wide(e,pointer);*last=e.last;return result;}
extern "C" unsigned print_error(unsigned value,unsigned*last){unsigned options[15]={};Environment e(options);Win98PrintDialog::Error(e,value);unsigned v=Win98PrintDialog::ExtendedError(e);*last=e.last;return v;}
#ifdef PRINT_DIALOG_CHECK
int main(){unsigned o[]={1,66,0,0,0,0x400,1,0,1,0,0,0,1,0,0};unsigned out[5],events[2048];unsigned char memory[65536];print_run(o,out,memory,events);assert(out[0]==0&&out[1]==0x1008);o[11]=1;o[7]=80;print_run(o,out,memory,events);assert(out[0]==1&&out[1]==0);return 0;}
#endif
