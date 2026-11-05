// SPDX-License-Identifier: GPL-2.0-or-later
// The host supplies one actual shared backing page to two separate Unicorn VMs.
// This tests global lock ownership; it does not claim a named shared-memory API.
#define private public
#include "../src/x86_app.h"
#undef private
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
static void require(bool ok,const char*why){if(!ok){fprintf(stderr,"GLOBAL CRITICAL FAILED: %s\n",why);exit(1);}}
static std::vector<unsigned char>load(const char*p){FILE*f=fopen(p,"rb");require(f,"input");fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);require(fread(b.data(),1,n,f)==unsigned(n),"read input");fclose(f);return b;}
static void log(const char*s){puts(s);}
int main(int argc,char**argv){
 require(argc==3,"usage: runner GLOBALCRITICAL.EXE fixedsys.fnt");auto exe=load(argv[1]),font=load(argv[2]);
 void*backing=nullptr;require(!posix_memalign(&backing,16384,16384),"shared page allocation");for(unsigned i=0;i<4096;++i)static_cast<unsigned*>(backing)[i]=0; // whole 16 KiB host page for FEXCore
 auto*producer=new X86::Application(log);auto*consumer=new X86::Application(log);
 require(producer->Start(exe.data(),exe.size(),font.data(),font.size(),"GLOBALCRITICAL.EXE","producer"),"producer start");
 require(consumer->Start(exe.data(),exe.size(),font.data(),font.size(),"GLOBALCRITICAL.EXE","consumer"),"consumer start");
 require(producer->cpu->MapHost(0x50000000,4096,X86::Prot::All,backing)==X86::CpuError::Ok,"producer shared backing");
 require(consumer->cpu->MapHost(0x50000000,4096,X86::Prot::All,backing)==X86::CpuError::Ok,"consumer shared backing");
 require(producer->virtual_memory.Track(0x50000000,4096,4)&&consumer->virtual_memory.Track(0x50000000,4096,4),"shared page access metadata");
 volatile unsigned*shared=static_cast<volatile unsigned*>(backing);bool blocked=false,released=false;unsigned producer_calls=0;
 const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
 while(std::chrono::steady_clock::now()<end&&consumer->Status()!=X86::Application::Exited){
  if(producer){producer->Pump(1);require(producer->Status()!=X86::Application::Failed,"producer runtime");}
  consumer->Pump(1);require(consumer->Status()!=X86::Application::Failed,"consumer runtime");
  if(consumer->threads[0].block==5){blocked=true;require(producer,"producer owns blocked lock");auto*p=producer->FindCritical(shared[1]);require(p&&consumer->FindCritical(shared[1])==p,"same actual native lock in both processes");require(p->data.owner==producer->threads[0].id&&p->waiting==1,"real owner and blocked waiter");}
  if(producer&&producer->Status()==X86::Application::Exited){require(!producer->ExitCode(),"producer guest checks");producer_calls=producer->ApiCalls();delete producer;producer=nullptr;require(consumer->FindCritical(shared[1]),"global lock survives creating process destruction");released=true;shared[0]=6;}
 }
 require(blocked&&released,"actual contention and creator teardown observed");require(consumer->Status()==X86::Application::Exited&&!consumer->ExitCode()&&shared[0]==7,"consumer acquired surviving lock and deleted it");require(!consumer->FindCritical(shared[1]),"global object removed on explicit DeleteCriticalSection");
 printf("GLOBAL CRITICAL PASS producer_calls=%u consumer_calls=%u shared_object=%08x\n",producer_calls,consumer->ApiCalls(),shared[1]);delete consumer;free(backing);return 0;
}
