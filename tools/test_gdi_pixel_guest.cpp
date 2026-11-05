// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
static std::vector<unsigned char>load(const char*p){FILE*f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);auto n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char**argv){assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);for(unsigned round=0;round<1;++round){unsigned before=Gdi::LiveObjects();{X86::Application app(nullptr);assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"GDIPIXEL.EXE"));for(unsigned n=0;n<10000&&app.Status()!=X86::Application::Exited&&app.Status()!=X86::Application::Failed;++n){app.Pump(32);if(app.Status()==X86::Application::Idle)std::this_thread::sleep_for(std::chrono::milliseconds(1));}if(app.Status()!=X86::Application::Exited||app.ExitCode())fprintf(stderr,"PIXEL status=%u code=%u error=%s\n",app.Status(),app.ExitCode(),app.Error());assert(app.Status()==X86::Application::Exited&&!app.ExitCode());printf("GDI pixel guest PASS round=%u calls=%u callbacks=%u\n",round,app.ApiCalls(),app.Callbacks());}assert(Gdi::LiveObjects()==before);}return 0;}
