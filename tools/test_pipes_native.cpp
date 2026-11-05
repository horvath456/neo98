// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include "../src/files.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <vector>
static std::vector<unsigned char> load(const char*p){FILE*f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char**argv){assert(argc==4);auto exe=load(argv[1]),font=load(argv[2]),child=load(argv[3]);assert(!Files::System().Seed("C:\\WINDOWS\\PIPECHILD.EXE",child.data(),child.size()));X86::Application app(nullptr);assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"PIPETEST.EXE"));auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);while(app.Status()!=X86::Application::Exited&&app.Status()!=X86::Application::Failed&&std::chrono::steady_clock::now()<deadline){app.Pump(32);X86::Application::PumpChildren(32);}printf("PIPE state=%u exit=%u calls=%u error=%s\n",app.Status(),app.ExitCode(),app.ApiCalls(),app.Error());fflush(stdout);assert(app.Status()==X86::Application::Exited&&!app.ExitCode());}
