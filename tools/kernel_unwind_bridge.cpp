// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_exceptions.h"
#include <cstring>
namespace {
constexpr unsigned B=0x20000000,F=B+0x2000,R=B+0x1100,C=B+0x1000;
struct Environment {
 unsigned memory[16384]{},*out;const unsigned *row;unsigned head=F,iterations=0;bool alive=true;
 unsigned Read(unsigned p){return memory[(p-B)/4];}void Write(unsigned p,unsigned v){memory[(p-B)/4]=v;}
 unsigned Head(){return head;}void WriteHead(unsigned v){head=v;}
 unsigned StackTop(){return F+0x100;}unsigned StackBottom(){return F;}
 bool Alive(){return alive;}bool Iteration(){return ++iterations<100;}
 bool FrameReadable(unsigned p){return p>=B&&p<=B+65528;}
 bool AlternateStack(unsigned){return false;}bool SystemHandlersOnly(){return row[6]!=0;}
 void Event(unsigned kind,unsigned a,unsigned b,unsigned c){unsigned n=out[4];out[5+n]=kind;out[6+n]=a;out[7+n]=b;out[8+n]=c;out[4]+=4;}
 unsigned Handler(unsigned,unsigned record,unsigned frame,unsigned context,unsigned &dispatcher){
  Event(1,frame,Read(record),Read(record+4));
  if(row[7])Write(context+176,0xabcdef01);
  dispatcher=F+16;return row[frame==F?1:2];
 }
 void Raise(unsigned code,unsigned previous,unsigned){Event(2,code,Read(previous),Read(previous+4));alive=false;}
 void Reraise(unsigned record,unsigned){Event(3,Read(record),Read(record+4),0);alive=false;}
};
}
extern "C" void kernel_unwind(const unsigned*row,unsigned*out){
 std::memset(out,0,256*4);Environment e{};e.out=out;e.row=row;
 e.Write(R,row[8]?0xc0000026:row[4]);e.Write(R+4,row[8]?0:row[3]);e.Write(C+176,0x12345678);
 for(unsigned i=0;i<3;++i){e.Write(F+i*16,i==2?~0u:F+(i+1)*16);e.Write(F+i*16+4,row[6]?0x100300:0x100300);}
 if(row[5])e.Write(F,F);
 unsigned target=row[0]==0?0:row[0]==4?~0u:F+(row[0]-1)*16;
 out[0]=Win98Exceptions::Unwind(e,target,R,C);out[1]=e.head;out[2]=e.Read(R+4);out[3]=e.Read(C+176);
}
#ifdef UNWIND_CORPUS_MAIN
#include <cstdio>
int main(int argc,char**argv){
 if(argc!=2)return 2;FILE*f=std::fopen(argv[1],"rb");if(!f)return 3;
 unsigned row[9],expected[256],actual[256],count=0;
 while(std::fread(row,sizeof row,1,f)==1){
  if(std::fread(expected,sizeof expected,1,f)!=1)return 4;
  kernel_unwind(row,actual);if(std::memcmp(expected,actual,sizeof actual))return 5;++count;
 }
 std::fclose(f);std::printf("UNWIND sanitized corpus PASS cases=%u\n",count);return count?0:6;
}
#endif
