/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#define private public
#include "../src/x86_app.h"
#undef private
#include <cassert>
#include <cstdio>
#include <chrono>
#include <vector>
static std::vector<unsigned char>load(const char*p){FILE*f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char**argv){
 assert(argc==4);auto exe=load(argv[1]),font=load(argv[2]);using App=X86::Application;
 // The installed original NE file must select the ARM implementation too.
 auto serial=load(argv[3]);
 assert(!Files::System().Seed("C:\\WINDOWS\\SYSTEM\\SERIALUI.DLL",serial.data(),serial.size()));
 // Two processes without a global Win16 lock: since PLAN.md step 8 neither
 // blocks the other, so both must finish inside the same budget.
 App*apps[]={new App(nullptr),new App(nullptr)};
 for(auto*app:apps)assert(app->Start(exe.data(),exe.size(),font.data(),font.size(),"WIN16MOD.EXE"));
 auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
 while(std::chrono::steady_clock::now()<end){
  bool done=true;
  for(auto*app:apps)if(app->Status()!=App::Exited){app->Pump(1);done=false;
   if(app->Status()==App::Failed){printf("WIN16 failed: %s\n",app->Error());fflush(stdout);}assert(app->Status()!=App::Failed);
  }
  if(done)break;
 }
 for(auto*app:apps){
  printf("WIN16 modules state=%u exit=%u error=%s\n",app->Status(),app->ExitCode(),app->Error());fflush(stdout);
  assert(app->Status()==App::Exited&&!app->ExitCode());for(auto&m:app->native_win16_modules)assert(!m.handle);delete app;
 }
 puts("WIN16 modules PASS: native QT exports, refcounts, thunk counter, system level of GetpWin16Lock");
}
