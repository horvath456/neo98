// SPDX-License-Identifier: GPL-2.0-or-later
#define private public
#include "../src/x86_app.h"
#undef private
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
static std::vector<unsigned char>load(const char*p){FILE*f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);auto n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char**argv){assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);for(unsigned round=0;round<1;++round){unsigned before=Gdi::LiveObjects();{X86::Application app(nullptr);assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"DEVCOLOR.EXE"));for(unsigned n=0;n<10000&&app.Status()!=X86::Application::Exited&&app.Status()!=X86::Application::Failed;++n){app.Pump(32);if(app.Status()==X86::Application::Idle)std::this_thread::sleep_for(std::chrono::milliseconds(1));}if(app.Status()!=X86::Application::Exited||app.ExitCode())fprintf(stderr,"PIXEL status=%u code=%u error=%s\n",app.Status(),app.ExitCode(),app.Error());assert(app.Status()==X86::Application::Exited&&!app.ExitCode());
 auto dc=User::GetDC(User::GetDesktopWindow());assert(dc);User::Render();assert(!User::Dirty());
 unsigned args[]={dc,15,15,0x123456};assert(app.GdiApi("SetPixel",args)==0x123456&&User::Dirty());User::Render();assert(!User::Dirty());
 args[1]=9999;assert(app.GdiApi("SetPixel",args)==~0u&&!User::Dirty());args[1]=16;args[3]=0x654321;assert(app.GdiApi("SetPixelV",args)&&User::Dirty());User::Render();
 unsigned move[]={dc,20,20,0},line[]={dc,24,20,0};assert(app.GdiApi("MoveToEx",move));assert(app.GdiApi("LineTo",line)&&User::Dirty());assert(User::ReleaseDC(User::GetDesktopWindow(),dc));
 printf("Device color guest PASS round=%u calls=%u callbacks=%u\n",round,app.ApiCalls(),app.Callbacks());}assert(Gdi::LiveObjects()==before);}return 0;}
