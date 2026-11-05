/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/x86_app.h"
#include <cassert>
#include <cstdio>
#include <vector>
static std::vector<unsigned char>load(const char*p){FILE*f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
static void log(const char*p){puts(p);}
int main(int argc,char**argv){assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);auto*a=new X86::Application(log),*b=new X86::Application(log);assert(a->Start(exe.data(),exe.size(),font.data(),font.size(),"CABINET.EXE","A"));assert(b->Start(exe.data(),exe.size(),font.data(),font.size(),"CABINET.EXE","B"));for(unsigned i=0;i<1000;++i){if(a->Status()!=X86::Application::Exited&&a->Status()!=X86::Application::Failed)a->Pump(4);if(b->Status()!=X86::Application::Exited&&b->Status()!=X86::Application::Failed)b->Pump(4);if((a->Status()==X86::Application::Exited||a->Status()==X86::Application::Failed)&&(b->Status()==X86::Application::Exited||b->Status()==X86::Application::Failed))break;}for(auto*app:{a,b}){if(app->Status()!=X86::Application::Exited||app->ExitCode()){fprintf(stderr,"CABINET FAIL state=%u code=%u %s\n",app->Status(),app->ExitCode(),app->Error());assert(false);}}printf("SHELL CABINET PASS A_calls=%u B_calls=%u\n",a->ApiCalls(),b->ApiCalls());delete b;delete a;assert(!User::LiveWindows());}
