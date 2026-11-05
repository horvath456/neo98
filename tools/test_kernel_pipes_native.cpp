// SPDX-License-Identifier: GPL-2.0-or-later
// Native ASan/UBSan runner for the KERNEL32 pipe, mailslot, device and
// Winsock-service probe. Seeds the WSOCK32
// stand-in the original CreateSocketHandle loads by name, runs the x86 guest
// and verifies that every listed export was reached through its import.
#define private public
#include "../src/x86_app.h"
#undef private
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>
using App=X86::Application;
static std::vector<unsigned char> load(const char *path){FILE*f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char **argv){
 assert(argc==5);auto exe=load(argv[1]),font=load(argv[2]),wsock=load(argv[4]);
 assert(!Files::System().Seed("C:\\WINDOWS\\SYSTEM\\WSOCK32.DLL",wsock.data(),wsock.size()));
 App app(nullptr);
 assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"KPIPES.EXE"));
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
 while(app.Status()!=App::Exited&&app.Status()!=App::Failed&&std::chrono::steady_clock::now()<deadline)app.Pump(64);
 printf("KERNEL-PIPES state=%u exit=%u calls=%u callbacks=%u error=%s\n",app.Status(),app.ExitCode(),app.ApiCalls(),app.Callbacks(),app.Error());
 FILE *names=fopen(argv[3],"r");assert(names);char name[128];unsigned covered=0,total=0;
 while(fgets(name,sizeof name,names)){name[strcspn(name,"\r\n")]=0;if(!*name)continue;++total;unsigned calls=0;
  for(unsigned i=0;i<app.import_count;++i)if(!strcmp(app.imports[i].dll,"KERNEL32.DLL")&&!strcmp(app.imports[i].name,name))calls+=app.imports[i].calls;
  if(calls)++covered;else printf("NOT REACHED %s\n",name);
 }
 fclose(names);printf("KERNEL-PIPES coverage=%u/%u\n",covered,total);fflush(stdout);
 assert(app.Status()==App::Exited&&app.ExitCode()==0&&total==18&&covered==total);
 // The mailslot slots and 0x66 handles of the exited process are released with it.
 assert(!app.OwnsKernelPipeHandle(0x66000101));
 puts("KERNEL-PIPES PASS");
}
