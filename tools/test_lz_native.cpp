// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include "../src/files.h"
#include <cassert>
#include <cstdio>
#include <vector>
static std::vector<unsigned char> load(const char*p){FILE*f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char**argv){assert(argc==4);auto exe=load(argv[1]),font=load(argv[2]),dll=load(argv[3]);assert(!Files::System().Seed("C:\\WINDOWS\\SYSTEM\\LZ32.DLL",dll.data(),dll.size()));X86::Application app(nullptr);assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"LZTEST.EXE"));for(unsigned i=0;i<10000&&app.Status()!=X86::Application::Exited&&app.Status()!=X86::Application::Failed;++i)app.Pump(32);printf("LZ original DLL state=%u exit=%u calls=%u error=%s\n",app.Status(),app.ExitCode(),app.ApiCalls(),app.Error());fflush(stdout);assert(app.Status()==X86::Application::Exited&&!app.ExitCode());}
