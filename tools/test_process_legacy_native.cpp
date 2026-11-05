/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/x86_app.h"
#include <cassert>
#include <cstdio>
#include <vector>
static void log(const char*p){puts(p);}
static std::vector<unsigned char>load(const char*path){FILE*f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char**argv){assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);assert(!Files::System().Seed("C:\\WINDOWS\\LEGACY.EXE",exe.data(),exe.size()));auto*a=new X86::Application(log);assert(a->Start(exe.data(),exe.size(),font.data(),font.size(),"PARENT.EXE","PARENT"));for(unsigned i=0;i<1000&&a->Status()!=X86::Application::Exited&&a->Status()!=X86::Application::Failed;++i)a->Pump(8);if(a->Status()!=X86::Application::Exited||a->ExitCode()){fprintf(stderr,"FAIL status=%u code=%u %s children=%u records=%u\n",a->Status(),a->ExitCode(),a->Error(),X86::Application::LiveChildren(),X86::Application::ChildRecords());assert(false);}unsigned calls=a->ApiCalls();delete a;for(unsigned i=0;i<10&&X86::Application::LiveChildren();++i)X86::Application::PumpChildren(8);assert(!X86::Application::LiveChildren()&&!X86::Application::ChildRecords()&&!User::LiveWindows());printf("LEGACY PROCESSES PASS parent_calls=%u\n",calls);}
