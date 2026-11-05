// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <cstdlib>
static std::vector<unsigned char> load(const char*p){FILE*f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);auto n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char**argv){assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);for(unsigned bad=0;bad<2;++bad){X86::Application app(getenv("NEO98_SEH_LOG")?+[](const char*s){fprintf(stderr,"%s\n",s);}:nullptr);assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"SEH.EXE",bad?"SEH.EXE !":"SEH.EXE"));for(unsigned i=0;i<1000&&app.Status()!=X86::Application::Failed&&app.Status()!=X86::Application::Exited;++i)app.Pump(32);if(!bad){if(app.Status()!=X86::Application::Exited||app.ExitCode())fprintf(stderr,"SEH status%u exit%u %s\n",app.Status(),app.ExitCode(),app.Error());assert(app.Status()==X86::Application::Exited&&!app.ExitCode());printf("SEH guest PASS calls=%u callbacks=%u\n",app.ApiCalls(),app.Callbacks());}else{assert(app.Status()==X86::Application::Failed&&strstr(app.Error(),"Unhandled exception"));printf("SEH unhandled guest rejected: %s\n",app.Error());}}}
