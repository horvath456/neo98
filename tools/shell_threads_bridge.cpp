// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_shell_threads.h"
#include <cstdio>
#include <cstring>
struct Environment {
 const unsigned*r;unsigned*out,n=0;Win98ShellThreads::StartData data{};
 void Trace(unsigned op,unsigned a=0,unsigned b=0){out[4+n++]=op;out[4+n++]=a;out[4+n++]=b;}
 unsigned TlsGet(){Trace(1);return r[4];}void TlsSet(unsigned v){Trace(2,v);}
 void Reference(unsigned){}
 void AddRef(unsigned v){Trace(3,v);}void Release(unsigned v){Trace(4,v);}
 void ProcessRef(unsigned&v){Trace(5);v=r[5];}
 int ComInitialize(){Trace(6);return int(r[6]);}void ComUninitialize(){Trace(7);}
 unsigned Invoke(unsigned p,unsigned a){Trace(8,p,a);return p==0x20001000?0x12345678:0x87654321;}
 unsigned Event(){Trace(9);return r[2]?11:0;}
 unsigned Thread(const Win98ShellThreads::StartData&v){data=v;Trace(10);return r[3]?12:0;}
 void Wait(unsigned v){Trace(11,v);out[1]=Win98ShellThreads::Worker(*this,data);}
 void Signal(unsigned v){Trace(12,v);}void Close(unsigned v){Trace(13,v);}
};
extern "C" void shell_threads(const unsigned*r,unsigned*out){memset(out,0,256*4);Environment e{r,out};unsigned result=0,p=0;
 switch(r[0]){case 0:result=Win98ShellThreads::Create(e,0x20001000,99,r[1],r[7]?0x20001010:0);break;case 1:result=Win98ShellThreads::Worker(e,{0x20001000,r[7]?0x20001010u:0u,11,99,r[1],r[4],r[5]});break;case 2:result=Win98ShellThreads::GetRef(e,p);out[2]=p;break;case 3:result=Win98ShellThreads::SetRef(e,r[4]);break;}
 out[0]=result;out[3]=e.n;
}
#ifdef SHELL_THREADS_CORPUS_MAIN
int main(int argc,char**argv){if(argc!=2)return 1;FILE*f=fopen(argv[1],"rb");if(!f)return 2;unsigned r[8],expected[256],actual[256],n=0;while(fread(r,sizeof r,1,f)){if(fread(expected,sizeof expected,1,f)!=1)return 3;shell_threads(r,actual);if(memcmp(expected,actual,sizeof actual))return 4;++n;}fclose(f);printf("Shell threads original corpus PASS %u cases\n",n);}
#endif
