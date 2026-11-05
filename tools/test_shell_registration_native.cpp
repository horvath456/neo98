// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <memory>
#include <cstdio>
#include <vector>
#define private public
#include "../src/x86_app.h"
#undef private
#include "../src/user.cpp"
static std::vector<unsigned char> load(const char*p){FILE*f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);auto n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char**argv){assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);
 {auto app_owner=std::make_unique<X86::Application>(nullptr);auto &app=*app_owner;assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"SHELLREG.EXE"));for(unsigned n=0;n<10000&&app.Status()!=X86::Application::Failed&&app.Status()!=X86::Application::Exited;++n)app.Pump(32);if(app.Status()!=X86::Application::Exited||app.ExitCode())fprintf(stderr,"shell registration state=%u code=%u error=%s\n",app.Status(),app.ExitCode(),app.Error());assert(app.Status()==X86::Application::Exited&&!app.ExitCode());printf("SHELL REGISTRATION GUEST PASS: calls=%u callbacks=%u\n",app.ApiCalls(),app.Callbacks());assert(!User::shell_registration.shell_record);}
 for(unsigned profile=0;profile<2;++profile){auto app_owner=std::make_unique<X86::Application>(nullptr);auto &app=*app_owner;assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"SHELLREG.EXE"));app.threads[0].suspended=1;assert(!app.ShellWindow()&&!User::shell_registration.shell_record&&!User::shell_registration.task);if(profile==1){assert(!User::windows[0].used);User::windows[0].generation=0x12345;}unsigned h=User::CreateWindow("STATIC","Shell",0x80000000,10,10,100,80,0,0,0,1);assert(h);assert(app.SetShellWindow(h)&&app.ShellWindow()==h);assert(User::Find(h)->nonclient_flags&0x10000000);unsigned task=User::shell_registration.task;assert(task==1);assert(app.SetShellWindow(0));assert(!(User::Find(h)->nonclient_flags&0x10000000)&&User::shell_registration.task==task);assert(app.SetShellWindow(h));}
 assert(!User::shell_registration.shell_record&&!User::shell_registration.task);puts("SHELL REGISTRATION NATIVE PASS: shared state, reentrancy, original flags, destruction and next-application reset");}
