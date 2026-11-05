// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include "fixture_native_modules.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
static void log(const char*p){puts(p);}
static std::vector<unsigned char>load(const char*path){FILE*f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char**argv){assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);auto*a=new X86::Application(log);assert(FixtureModules::KeepNative(*a));assert(a->Start(exe.data(),exe.size(),font.data(),font.size(),"CRTOUTPUT.EXE","TEST"));for(unsigned i=0;i<400&&a->Status()!=X86::Application::Failed&&a->Status()!=X86::Application::Exited;++i)a->Pump(8);bool okay=a->Status()==X86::Application::Exited&&!a->ExitCode();unsigned calls=a->ApiCalls();printf("CRT OUTPUT %s state=%u code=%u calls=%u error=%s\n",okay?"PASS":"FAIL",a->Status(),a->ExitCode(),calls,a->Error());delete a;if(!okay)return 1;
 a=new X86::Application(log);assert(FixtureModules::KeepNative(*a));assert(a->Start(exe.data(),exe.size(),font.data(),font.size(),"CRTOUTPUT.EXE","FLOAT"));for(unsigned i=0;i<50&&a->Status()!=X86::Application::Failed&&a->Status()!=X86::Application::Exited;++i)a->Pump(8);okay=a->Status()==X86::Application::Failed&&strstr(a->Error(),"floating output conversion provider not ported");printf("CRT OUTPUT FLOAT %s calls=%u error=%s\n",okay?"PASS":"FAIL",a->ApiCalls(),a->Error());delete a;return okay?0:1;
}
