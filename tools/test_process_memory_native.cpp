// SPDX-License-Identifier: GPL-2.0-or-later
#define private public
#include "../src/x86_app.h"
#undef private
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>
#include <memory>
using App=X86::Application;
static std::vector<unsigned char> load(const char*p){FILE*f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char**argv){
 assert(argc==4);auto exe=load(argv[1]),font=load(argv[2]);
 {
  auto owned_app=std::make_unique<App>(nullptr);
  auto &app=*owned_app;assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"MEMORY.EXE","MEMORY.EXE caf\xe9"));
  auto end=std::chrono::steady_clock::now()+std::chrono::seconds(15);
  while(app.Status()!=App::Exited&&app.Status()!=App::Failed&&std::chrono::steady_clock::now()<end)app.Pump(32);
  printf("PROCESS MEMORY GUEST state=%u exit=%u calls=%u error=%s\n",app.Status(),app.ExitCode(),app.ApiCalls(),app.Error());fflush(stdout);
  assert(app.Status()==App::Exited&&!app.ExitCode());
 }
 {
  auto owned_caller=std::make_unique<App>(nullptr),owned_target=std::make_unique<App>(nullptr);
  auto &caller=*owned_caller,&target=*owned_target;target.parent_process=&caller;
  assert(caller.Start(exe.data(),exe.size(),font.data(),font.size(),"CALLER.EXE"));
  assert(target.Start(exe.data(),exe.size(),font.data(),font.size(),"TARGET.EXE"));
  caller.threads[0].suspended=target.threads[0].suspended=1;
  auto corpus=load(argv[3]);assert(corpus.size()>=44);
  const auto word=[&](unsigned offset){unsigned value;assert(offset+4<=corpus.size());memcpy(&value,corpus.data()+offset,4);return value;};
  const unsigned signature=caller.cpu_signature;
  caller.cpu_signature=word(0);
  const unsigned scratch=caller.Allocate(64),system[]={scratch};
  caller.ProcessMemoryApi("GetSystemInfo",system);
  for(unsigned i=0;i<9;++i)assert(caller.Read32(scratch+i*4)==word(4+i*4));
  caller.cpu_signature=signature;caller.Free(scratch);
  for(unsigned i=0;i<word(40);++i){
   const unsigned args[]={word(44+i*12),word(48+i*12)};
   caller.last_error=0x1234;
   assert(caller.ProcessMemoryApi("VirtualUnlock",args)==word(52+i*12)&&caller.last_error==0x1234);
  }
  unsigned open[]={0x1f0fff,0,target.ProcessId()};unsigned handle=caller.OpenProcessApi(open);assert(handle&&handle!=0x7fffffff);
  const unsigned alloc[]={0,8192,0x3000,4};unsigned local=caller.MemoryApi("VirtualAlloc",alloc),remote=target.MemoryApi("VirtualAlloc",alloc);assert(local&&remote);
  // Identical VAs in separate guest spaces must not alias.
  assert(local==remote);caller.Write32(local,0x11112222);target.Write32(remote,0xaabbccdd);
  unsigned args[]={handle,remote,local+4096,4,local+4100};target.last_error=0x1357;
  assert(caller.ProcessMemoryApi("ReadProcessMemory",args)&&caller.Read32(local+4096)==0xaabbccdd&&caller.Read32(local+4100)==4&&target.last_error==0x1357);
  caller.Write32(local+4096,0x76543210);assert(caller.ProcessMemoryApi("WriteProcessMemory",args)&&target.Read32(remote)==0x76543210&&caller.Read32(local)==0x11112222);
  unsigned query[]={handle,remote,local+4200,28};assert(caller.ProcessMemoryApi("VirtualQueryEx",query)==28&&caller.Read32(local+4220)==4);
  unsigned protect[]={handle,remote,4096,2,local+4250};assert(caller.ProcessMemoryApi("VirtualProtectEx",protect)==4&&caller.Read32(local+4250)==4);
  assert(!caller.ProcessMemoryApi("WriteProcessMemory",args)&&caller.last_error==299&&target.last_error==0x1357);
  assert(caller.virtual_memory.Access(local,4,true)&&!target.virtual_memory.Access(remote,4,true));
  protect[3]=4;assert(caller.ProcessMemoryApi("VirtualProtectEx",protect)==2);
  unsigned invalid[]={handle,remote,4096,0x80000000,local+4250};assert(!caller.ProcessMemoryApi("VirtualProtectEx",invalid)&&caller.last_error==87&&target.last_error==0x1357);
  invalid[0]=0;assert(!caller.ProcessMemoryApi("VirtualProtectEx",invalid)&&caller.last_error==87);
  const unsigned close[]={handle};assert(caller.ThreadApi("CloseHandle",close));
  assert(!caller.ProcessMemoryApi("ReadProcessMemory",args)&&caller.last_error==6);
  puts("PROCESS MEMORY CROSS-PROCESS PASS: distinct address spaces, protection, output ownership, errors and stale handles");
 }
}
