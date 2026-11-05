// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include "fixture_native_modules.h"
#include <cassert>
#include <memory>
#include <cstdio>
#include <cstring>
#include <vector>
static std::vector<unsigned char>load(const char*p){FILE*f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char**argv){assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);unsigned calls=0;
 {auto app_storage=std::make_unique<X86::Application>(nullptr);auto &app=*app_storage;assert(FixtureModules::KeepNative(app));assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"SHELLSW.EXE"));for(unsigned i=0;i<1000&&app.Status()!=X86::Application::Exited&&app.Status()!=X86::Application::Failed;++i)app.Pump(32);if(app.Status()!=X86::Application::Exited||app.ExitCode())fprintf(stderr,"StopWatch state=%u exit=%u calls=%u error=%s\n",app.Status(),app.ExitCode(),app.ApiCalls(),app.Error());assert(app.Status()==X86::Application::Exited&&!app.ExitCode());calls=app.ApiCalls();}
 // Configure the actual shared registry before the next module attach. The
 // native provider must reject activation, including Mode=0 (original forces1).
 Registry::Session registry(Registry::System());unsigned key=0,node=0,mode=0;bool created=false;const char*path="Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Performance";assert(!registry.Open(0x80000002,path,true,key,created));assert(!registry.Key(key,node));assert(!registry.store.Set(node,"Mode",4,&mode,4));assert(!registry.Close(key));
 {auto app_storage=std::make_unique<X86::Application>(nullptr);auto &app=*app_storage;assert(FixtureModules::KeepNative(app));assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"SHELLSW.EXE"));for(unsigned i=0;i<1000&&app.Status()!=X86::Application::Failed&&app.Status()!=X86::Application::Exited;++i)app.Pump(32);assert(app.Status()==X86::Application::Failed);assert(strstr(app.Error(),"Configured SHLWAPI StopWatch profiling provider"));calls+=app.ApiCalls();}
 assert(!registry.store.DeleteKey(registry.store.roots[2],path));printf("Original SHLWAPI StopWatch PASS calls=%u; normal attach/cache/reload and explicit configured-logger boundary\n",calls);
}
