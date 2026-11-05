// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_exceptions.h"
#include <cstring>
#include <cstdio>
using U=unsigned;
namespace {constexpr U B=0x20000000,C=B+0x1000,R=B+0x1100,F=B+0x2000,P=B+0x1200,S=B+0x49c;
struct E {
 unsigned char memory[65536]{};U trace[256]{},used=0;const U*row;bool alive=true;
 U Read(U p){U v;memcpy(&v,memory+(p-B),4);return v;}void Write(U p,U v){memcpy(memory+(p-B),&v,4);}
 void Copy(U d,U s,U n){memcpy(memory+(d-B),memory+(s-B),n);}
 void Event(U a,U b=0,U c=0,U d=0){trace[used++]=a;trace[used++]=b;trace[used++]=c;trace[used++]=d;}
 bool Alive(){return alive;}bool Iteration(){return true;}U Head(){return row[1]?F:~0u;}U StackTop(){return F+0x100;}U StackBottom(){return F;}
 bool FrameReadable(U){return true;}bool AlternateStack(U){return false;}bool SystemHandlersOnly(){return row[8];}
 bool Debug(bool first,U trap,U r,U c){Event(1,first,trap,r);return row[first?6:7];}bool KernelDebug(U){return false;}
 bool Debugged(){return row[6];}bool ThreadFaultReported(){return row[7];}void ReportThreadFault(){}void ClearThreadFault(){}
 U Handler(U proc,U record,U frame,U context,U&dispatcher){U index=(frame-F)/16;Event(2,proc,frame,Read(record+4));dispatcher=row[5]?F+16:0;if(row[9])Write(context+0xb0,0x12345678);return row[index?3:2];}
 void Raise(U code,U record,U){Event(3,code,record);alive=false;}
 void Unhandled(U record,U context){Event(4,record,context);}
 Win98Exceptions::Result Terminate(U){Event(5);return Win98Exceptions::Stop;}
 Win98Exceptions::Result VmEnd(U trap,U context){Event(6,trap,context);return Win98Exceptions::Stop;}
 bool RepairWriteFault(U address){Event(7,address);return row[9];}U Filter(){return Read(S);}
 U FilterCall(U proc,U pointers){Event(8,proc,pointers);return row[2];}bool SuppressFaultBox(){return row[8];}
 U DefaultFilter(U pointers,U result){Event(9,pointers,result);return row[3];}
};}
extern "C" void kernel_exceptions(const U*row,U*out){
 E e;e.row=row;memset(e.memory,0,sizeof e.memory);e.Write(C+0xc0,0x302);e.Write(C+0xb0,0xaabbccdd);e.Write(R,row[4]);e.Write(R+4,row[10]);e.Write(R+20,row[11]);e.Write(R+24,0x1234);e.Write(P,R);e.Write(P+4,C);e.Write(S,row[1]?0x100400:0);
 e.Write(F,F+16);e.Write(F+4,row[8]?0xbff70000:0x100300);e.Write(F+16,row[1]==3?F:~0u);e.Write(F+20,row[8]?0xbff70010:0x100310);
 U result;
 if(row[0]==0)result=Win98Exceptions::Dispatch(e,C,R,~0u);
 else if(row[0]==1)result=Win98Exceptions::Unhandled(e,P);
 else if(row[0]==2){for(U i=0;i<15;++i)e.Write(F+i*4,0x11110000+i);Win98Exceptions::Record(e,R,row[4],row[10],row[2],row[1]?F:0);result=0;}
 else if(row[0]==3)result=Win98Exceptions::SetFilter(e,S,row[2]);
 else {e.Write(F+8,F+16);result=Win98Exceptions::Nested(e,R,F,P);}
 out[0]=result;out[1]=e.used;memcpy(out+2,e.trace,sizeof e.trace);memcpy(reinterpret_cast<unsigned char*>(out+258),e.memory,sizeof e.memory);
}
#ifdef EXCEPTIONS_CORPUS_MAIN
int main(int argc,char**argv){if(argc!=2)return 2;FILE*f=fopen(argv[1],"rb");if(!f)return 2;U row[12],expected[16642],got[16642],n=0;while(fread(row,sizeof row,1,f)==1){if(fread(expected,sizeof expected,1,f)!=1)return 2;kernel_exceptions(row,got);if(memcmp(expected,got,sizeof got)){fprintf(stderr,"SEH row%u failed\n",n);return 1;}++n;}fclose(f);printf("SEH ORIGINAL CORPUS %u PASS\n",n);}
#endif
