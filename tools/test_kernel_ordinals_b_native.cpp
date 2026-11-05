// SPDX-License-Identifier: GPL-2.0-or-later
// Runner for tools/kernel_ordinals_b_fixture.c: the default mode must exit with
// code zero. Additional modes exercise native services and the original divide fault.
// Import coverage is summed over all runs.
#define private public
#include "../src/x86_app.h"
#undef private
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using App=X86::Application;
static std::vector<unsigned char> load(const char *path){FILE*f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
struct Mode{const char *command,*expect;bool success=false;};
static const Mode modes[]={
 {nullptr,nullptr},
 {"mode=89",nullptr,true},
 {"mode=90",nullptr,true},
 {"mode=101",nullptr,true},
 {"mode=104",nullptr,true},
 {"mode=121",nullptr,true},
 {"mode=122",nullptr,true},
 {"mode=75",nullptr},
};
int main(int argc,char **argv){
 assert(argc==4);auto exe=load(argv[1]),font=load(argv[2]);
 std::map<std::string,unsigned> calls;
 for(const auto &mode:modes){
  App app(nullptr);
  assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"KERNEL_ORDINALS_B.EXE",mode.command));
  if(mode.command&&!strcmp(mode.command,"mode=122")){
   auto&store=app.files.store;assert(!store.MakeDirectory("C:\\WINDOWS\\APPLOG"));assert(!store.MakeDirectory("C:\\APPS"));
   const char payload[]="x";assert(!store.Seed("C:\\APPS\\KEEP0001.EXE",payload,1));assert(!store.Seed("C:\\APPS\\KEEP0002.EXE",payload,1));
   unsigned char bytes[512]={};auto put=[&](unsigned offset,unsigned value){for(unsigned i=0;i<4;++i)bytes[offset+i]=value>>(8*i);};
   memcpy(bytes,"APPLOG1.1",10);put(16,3);put(20,8);put(24,0x12345678);put(28,0x98765432);
   put(64,0);put(68,2);put(72,2);put(76,1);
   memcpy(bytes+408,"KEEP0001",8);memcpy(bytes+440,"DROP0001",8);memcpy(bytes+472,"KEEP0002",8);
   put(424,(app.CurrentFileTime()>>32)+1);put(456,(app.CurrentFileTime()>>32)+1);put(488,0);bytes[438]=255;
   memcpy(bytes+504,"C:\\APPS",8);assert(!store.Seed("C:\\WINDOWS\\APPLOG\\APPLOG.ind",bytes,sizeof bytes));
  }
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
  while(app.Status()!=App::Exited&&app.Status()!=App::Failed&&std::chrono::steady_clock::now()<deadline)app.Pump(64);
  printf("KERNEL-ORDINALS-B mode=%s state=%u exit=%u calls=%u callbacks=%u error=%s\n",mode.command?mode.command:"default",app.Status(),app.ExitCode(),app.ApiCalls(),app.Callbacks(),app.Error());
  if(mode.command&&!strcmp(mode.command,"mode=101")){assert(Files::CacheResidentPages()==128&&Files::CacheBufferLoans()>=2);}
  if(mode.command&&!strcmp(mode.command,"mode=122")){
   unsigned node=0;assert(!app.files.store.Lookup("C:\\WINDOWS\\APPLOG\\APPLOG.ind",node));auto*f=app.files.store.Get(node);
   assert(f&&f->size==448);auto get=[&](unsigned offset){unsigned v=0;memcpy(&v,f->data+offset,4);return v;};
   assert(get(16)==1&&get(20)==8&&get(24)==0x12345678&&get(28)==0x98765432);
   assert(get(64)==0&&get(68)==1&&get(72)==1&&get(76)==0);
   assert(!memcmp(f->data+408,"KEEP0001",8)&&!memcmp(f->data+440,"C:\\APPS",8)&&!f->data[438]);
  }
  for(unsigned i=0;i<app.import_count;++i)if(!strcmp(app.imports[i].dll,"KERNEL32.DLL"))calls[app.imports[i].name]+=app.imports[i].calls;
  if(!mode.command||mode.success){assert(app.Status()==App::Exited&&app.ExitCode()==0);}
  else if(mode.expect){assert(app.Status()==App::Failed&&!strncmp(app.Error(),mode.expect,strlen(mode.expect)));}
  else{
   // mode=75: the divide fault reaches the guest as STATUS_INTEGER_DIVIDE_BY_ZERO;
   // the fixture installs no handler, so the run must not finish normally.
   assert(!(app.Status()==App::Exited&&app.ExitCode()==0));
   assert(app.Status()==App::Failed||app.ExitCode()==0xc0000094u);
  }
 }
 FILE *names=fopen(argv[3],"r");assert(names);char name[128];unsigned covered=0,total=0;
 while(fgets(name,sizeof name,names)){name[strcspn(name,"\r\n")]=0;if(!*name)continue;++total;if(calls[name])++covered;else printf("NOT REACHED %s\n",name);}
 fclose(names);printf("KERNEL-ORDINALS-B coverage=%u/%u\n",covered,total);fflush(stdout);
 assert(total>=60&&covered==total);
 puts("KERNEL-ORDINALS-B PASS");
}
