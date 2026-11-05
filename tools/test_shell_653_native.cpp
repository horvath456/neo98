/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/x86_app.h"
#include "fixture_native_modules.h"
#include <cassert>
#include <cstdio>
#include <vector>
static std::vector<unsigned char> load(const char*path){FILE*f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char**argv){assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);auto&files=Files::System();for(auto*p:{"C:\\FIRST","C:\\SECOND","C:\\TOOLS","C:\\My Tools"})assert(!files.MakeDirectory(p));const char bytes[]="fixture";for(auto*p:{"C:\\FIRST\\tool.pif","C:\\SECOND\\tool.exe","C:\\TOOLS\\tool.exe","C:\\My Tools\\paint.exe","C:\\WINDOWS\\SYSTEM\\thing.com","C:\\WINDOWS\\thing.exe"})assert(!files.Seed(p,bytes,sizeof bytes));X86::Application app(nullptr);assert(FixtureModules::KeepNative(app));assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"SHELL653.EXE"));for(unsigned n=0;n<5000&&app.Status()!=X86::Application::Failed&&app.Status()!=X86::Application::Exited;++n)app.Pump(16);if(app.Status()!=X86::Application::Exited||app.ExitCode())fprintf(stderr,"SHELL653 status=%u code=%u error=%s\n",app.Status(),app.ExitCode(),app.Error());assert(app.Status()==X86::Application::Exited&&app.ExitCode()==0);printf("SHELL653 NATIVE PASS calls=%u callbacks=%u\n",app.ApiCalls(),app.Callbacks());}
