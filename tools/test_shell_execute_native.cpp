/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/x86_app.h"
#include "fixture_native_modules.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <string>
static std::vector<std::string> messages;
static void log(const char*p){messages.emplace_back(p);puts(p);}
static std::vector<unsigned char> load(const char*path){FILE*f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char**argv){assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);auto&files=Files::System();assert(!files.MakeDirectory("C:\\TOOLS"));assert(!files.Seed("C:\\TOOLS\\tool.exe",exe.data(),exe.size()));const char text[]="document";assert(!files.Seed("C:\\TOOLS\\document.xyz",text,sizeof text));X86::Application app(log);assert(FixtureModules::KeepNative(app));assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"SHELLEXEC.EXE"));for(unsigned n=0;n<5000&&app.Status()!=X86::Application::Failed&&app.Status()!=X86::Application::Exited;++n)app.Pump(16);if(app.Status()!=X86::Application::Exited||app.ExitCode())fprintf(stderr,"SHELLEXEC status=%u code=%u error=%s\n",app.Status(),app.ExitCode(),app.Error());assert(app.Status()==X86::Application::Exited&&app.ExitCode()==0);bool edit=false,changed=false,alias=false;for(auto&s:messages){edit|=s=="tool.exe /edit \"C:\\TOOLS\\document.xyz\" first arg /show:3";changed|=s=="tool.exe /changed C:\\TOOLS\\document.xyz";alias|=s=="\"C:\\TOOLS\\tool.exe\" --alias";}assert(edit&&changed&&alias);for(unsigned n=0;n<500&&X86::Application::LiveChildren();++n)X86::Application::PumpChildren(4);assert(!X86::Application::LiveChildren()&&!X86::Application::ChildRecords());for(const char*marker:{"direct","alias","edit","changed"}){std::string path="C:\\TOOLS\\";path+=marker;path+=".done";unsigned node=0;assert(!files.Lookup(path.c_str(),node)&&files.Get(node)->size>0);}printf("SHELLEXEC NATIVE PASS calls=%u callbacks=%u\n",app.ApiCalls(),app.Callbacks());}
