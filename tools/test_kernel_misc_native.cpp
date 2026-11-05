// SPDX-License-Identifier: GPL-2.0-or-later
// Runner for the KERNEL32 fiber/debug/power family guest.
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
static std::map<std::string,unsigned> calls;
static void run(const std::vector<unsigned char>&exe,const std::vector<unsigned char>&font,const char*mode,App::State expected,unsigned exit_code,const char*message){
 App app(nullptr);std::string command=std::string("KERNELMISC.EXE ")+mode;
 assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"KERNELMISC.EXE",command.c_str()));
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
 while(app.Status()!=App::Exited&&app.Status()!=App::Failed&&std::chrono::steady_clock::now()<deadline)app.Pump(64);
 printf("KERNELMISC mode=%s state=%u exit=%u calls=%u callbacks=%u error=%s\n",*mode?mode:"(all)",app.Status(),app.ExitCode(),app.ApiCalls(),app.Callbacks(),app.Error());
 for(unsigned i=0;i<app.import_count;++i)if(!strcmp(app.imports[i].dll,"KERNEL32.DLL"))calls[app.imports[i].name]+=app.imports[i].calls;
 fflush(stdout);
 assert(app.Status()==expected);
 if(expected==App::Exited)assert(app.ExitCode()==exit_code);
 if(message)assert(strstr(app.Error(),message));
}
int main(int argc,char **argv){
 assert(argc==4);auto exe=load(argv[1]),font=load(argv[2]);
 run(exe,font,"",App::Exited,0,nullptr);
 run(exe,font,"fatalapp",App::Exited,0xffffffffu,nullptr);
 run(exe,font,"fatalexit",App::Exited,0xffffffffu,nullptr);
 run(exe,font,"break",App::Failed,0,"80000003");
 FILE *names=fopen(argv[3],"r");assert(names);char name[128];unsigned covered=0,total=0;
 while(fgets(name,sizeof name,names)){name[strcspn(name,"\r\n")]=0;++total;if(calls[name])++covered;else printf("NOT REACHED %s\n",name);}
 fclose(names);printf("KERNELMISC coverage=%u/%u\n",covered,total);fflush(stdout);
 assert(total==33&&covered==total);
 puts("KERNELMISC PASS");
}
