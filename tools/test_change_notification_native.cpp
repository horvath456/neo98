// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
static void require(bool ok,const char*why){if(!ok){fprintf(stderr,"CHANGE NOTIFICATION FAILED: %s\n",why);exit(1);}}
static std::vector<unsigned char>load(const char*p){FILE*f=fopen(p,"rb");require(f,"input");fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);require(fread(b.data(),1,n,f)==unsigned(n),"read");fclose(f);return b;}
int main(int argc,char**argv){
 require(argc==3,"usage: runner CHANGENO.EXE fixedsys.fnt");
 auto exe=load(argv[1]),font=load(argv[2]);
 unsigned idle=0;
 {
  X86::Application app(nullptr);
  require(app.Start(exe.data(),exe.size(),font.data(),font.size(),"CHANGENO.EXE"),"start");
  auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
  while(app.Status()!=X86::Application::Exited&&app.Status()!=X86::Application::Failed&&std::chrono::steady_clock::now()<end){
   if(app.Pump()==X86::Application::Idle)++idle;
  }
  printf("CHANGE NOTIFICATION state=%u exit=%u calls=%u idle=%u\n",app.Status(),app.ExitCode(),app.ApiCalls(),idle);
  require(app.Status()==X86::Application::Exited&&!app.ExitCode(),app.Error());
 }
 puts("CHANGE NOTIFICATION PASS");
}
