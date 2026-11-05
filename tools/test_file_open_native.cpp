// SPDX-License-Identifier: GPL-2.0-or-later
#define private public
#include "../src/x86_app.h"
#undef private
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
static void require(bool ok,const char*why){if(!ok){fprintf(stderr,"FILE OPEN FAILED: %s\n",why);exit(1);}}
static X86::Application*observed=nullptr;static unsigned inheritance_checks=0;
static void log(const char*s){
 if(observed&&(!strcmp(s,"WriteFile")||!strcmp(s,"ReadFile"))){
  unsigned handle=observed->Read32(observed->Register(X86::Reg::Esp)+4);
  require(observed->files.IsInheritable(handle)==!strcmp(s,"WriteFile"),"actual file handle inheritance metadata");++inheritance_checks;
 }
}
static std::vector<unsigned char>load(const char*p){FILE*f=fopen(p,"rb");require(f,"input");fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);require(fread(b.data(),1,n,f)==unsigned(n),"read");fclose(f);return b;}
int main(int argc,char**argv){
 require(argc==3,"usage: runner FILEOPEN.EXE fixedsys.fnt");auto exe=load(argv[1]),font=load(argv[2]);X86::Application app(log);observed=&app;
 require(app.Start(exe.data(),exe.size(),font.data(),font.size(),"FILEOPEN.EXE"),"start");
 const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(6);
 while(app.Status()!=X86::Application::Exited&&app.Status()!=X86::Application::Failed&&std::chrono::steady_clock::now()<end)app.Pump();
 printf("FILE OPEN state=%u exit=%u calls=%u inheritance_checks=%u\n",app.Status(),app.ExitCode(),app.ApiCalls(),inheritance_checks);
 require(app.Status()==X86::Application::Exited&&!app.ExitCode()&&inheritance_checks==2,"original parameters and native file operations");observed=nullptr;puts("FILE OPEN PASS");
}
