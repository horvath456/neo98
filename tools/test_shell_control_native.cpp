/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/x86_app.h"
#include <cassert>
#include <cstdio>
#include <vector>
static std::vector<unsigned char>load(const char*p){FILE*f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
static void log(const char*p){puts(p);}
int main(int argc,char**argv){assert(argc==4);auto exe=load(argv[1]),cpl=load(argv[2]),font=load(argv[3]);assert(!Files::System().Seed("C:\\WINDOWS\\SYSTEM\\FIXTURE.CPL",cpl.data(),cpl.size()));auto*app=new X86::Application(log);assert(app->Start(exe.data(),exe.size(),font.data(),font.size(),"CPLTEST.EXE"));for(unsigned i=0;i<1000&&app->Status()!=X86::Application::Exited&&app->Status()!=X86::Application::Failed;++i)app->Pump(8);if(app->Status()!=X86::Application::Exited||app->ExitCode()){fprintf(stderr,"CPL FAIL state=%u code=%u %s\n",app->Status(),app->ExitCode(),app->Error());assert(false);}printf("SHELL CONTROL PASS calls=%u callbacks=%u\n",app->ApiCalls(),app->Callbacks());delete app;assert(!User::LiveWindows());}
