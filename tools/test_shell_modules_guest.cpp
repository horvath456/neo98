// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include "fixture_native_modules.h"
#include <cassert>
#include <cstdio>
#include <vector>
static std::vector<unsigned char> load(const char*p){FILE*f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char**argv){assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);X86::Application app(nullptr);assert(FixtureModules::KeepNative(app));assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"SHELLMOD.EXE"));for(unsigned i=0;i<1000&&app.Status()!=X86::Application::Exited&&app.Status()!=X86::Application::Failed;++i)app.Pump(32);if(app.Status()!=X86::Application::Exited||app.ExitCode())fprintf(stderr,"Shell modules state=%u exit=%u calls=%u error=%s\n",app.Status(),app.ExitCode(),app.ApiCalls(),app.Error());assert(app.Status()==X86::Application::Exited&&!app.ExitCode());printf("Original shell module wrappers PASS calls=%u callbacks=%u\n",app.ApiCalls(),app.Callbacks());}
