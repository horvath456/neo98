// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
static void require(bool ok,const char*why){if(!ok){fprintf(stderr,"KERNEL COMPARE FAULT FAILED: %s\n",why);exit(1);}}
static std::vector<unsigned char>load(const char*p){FILE*f=fopen(p,"rb");require(f,"input");fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);require(fread(b.data(),1,n,f)==unsigned(n),"read");fclose(f);return b;}
static void log(const char*s){puts(s);}
int main(int argc,char**argv){require(argc==3,"usage: runner CMPFAULT.EXE fixedsys.fnt");auto exe=load(argv[1]),font=load(argv[2]);for(unsigned process=0;process<1;++process){X86::Application app(log);require(app.Start(exe.data(),exe.size(),font.data(),font.size(),"CMPFAULT.EXE"),"start");const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(6);while(app.Status()!=X86::Application::Exited&&app.Status()!=X86::Application::Failed&&std::chrono::steady_clock::now()<end)app.Pump();printf("KERNEL COMPARE FAULT process=%u state=%u exit=%u calls=%u\n",process,app.Status(),app.ExitCode(),app.ApiCalls());require(app.Status()==X86::Application::Failed&&app.ApiCalls()==3&&strstr(app.Error(),"Guest read violates page protection"),"huge counted comparison stopped at uncommitted page");}puts("KERNEL COMPARE FAULT PASS");return 0;}
