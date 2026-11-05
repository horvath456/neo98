// SPDX-License-Identifier: GPL-2.0-or-later
// Runs the thunk-bridge fixture and checks that every named export was
// reached through its public import.
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
static unsigned transitions,dispatched,faults,pages;
static void logger(const char *text){
 if(!strncmp(text,"FLATTHUNK transition",20))++transitions;
 if(!strncmp(text,"FLATTHUNK dispatched",20))++dispatched;
 if(!strncmp(text,"FLATTHUNK fault",15))++faults;
 if(!strncmp(text,"CALLBACKPAGE",12))++pages;
 if(!strncmp(text,"FLATTHUNK",9)||!strncmp(text,"CALLBACKPAGE",12)||!strncmp(text,"UTREGISTER",10))puts(text);
}
int main(int argc,char **argv){
 assert(argc==4);auto exe=load(argv[1]),font=load(argv[2]);App app(logger);
 assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"KTHUNKS.EXE"));
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
 while(app.Status()!=App::Exited&&app.Status()!=App::Failed&&std::chrono::steady_clock::now()<deadline)app.Pump(64);
 printf("KTHUNKS state=%u exit=%u calls=%u error=%s transitions=%u dispatched=%u faults=%u pages=%u\n",app.Status(),app.ExitCode(),app.ApiCalls(),app.Error(),transitions,dispatched,faults,pages);
 FILE *names=fopen(argv[3],"r");assert(names);char name[128];unsigned covered=0,total=0;
 while(fgets(name,sizeof name,names)){name[strcspn(name,"\r\n")]=0;++total;unsigned calls=0;
  for(unsigned i=0;i<app.import_count;++i)if(!strcmp(app.imports[i].dll,"KERNEL32.DLL")&&!strcmp(app.imports[i].name,name))calls+=app.imports[i].calls;
  if(calls)++covered;else printf("NOT REACHED %s\n",name);
 }
 fclose(names);printf("KTHUNKS coverage=%u/%u\n",covered,total);fflush(stdout);
 assert(app.Status()==App::Exited&&app.ExitCode()==0&&total==61&&covered==total);
 // Twenty thunk calls dispatched, four faults resumed, three callback pages.
 assert(dispatched==20&&transitions==20&&faults==4&&pages==3);
 puts("KTHUNKS PASS");
}
