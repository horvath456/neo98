// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include "fixture_native_modules.h"
#include <cstdio>
#include <cassert>
#include <vector>
#include <cstring>
static std::vector<unsigned char>load(const char*p){FILE*f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char**argv){assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);for(unsigned pass=0;pass<2;++pass){X86::Application app(nullptr);assert(FixtureModules::KeepNative(app));assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"SHELL509.EXE",pass?"2":"1"));for(unsigned n=0;n<10000&&app.Status()!=X86::Application::Failed&&app.Status()!=X86::Application::Exited;++n)app.Pump(32);if(pass){assert(app.Status()==X86::Application::Failed&&strstr(app.Error(),"260-byte ANSI frame"));puts("SHELL509 oversized Unicode name boundary PASS");continue;}if(app.Status()!=X86::Application::Exited||app.ExitCode())fprintf(stderr,"Shell ordinals status=%u code=%u error=%s\n",app.Status(),app.ExitCode(),app.Error());assert(app.Status()==X86::Application::Exited&&!app.ExitCode());printf("SHELL509 NATIVE PASS calls=%u callbacks=%u\n",app.ApiCalls(),app.Callbacks());}assert(!User::LiveWindows()&&!Gdi::LiveObjects());}
