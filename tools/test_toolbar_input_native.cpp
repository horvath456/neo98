/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/x86_app.h"
#include "fixture_native_modules.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <cstring>
static void log(const char*p){puts(p);}
static std::vector<unsigned char>load(const char*path){FILE*f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
static void pump(X86::Application&a){for(unsigned i=0;i<24&&a.Status()!=X86::Application::Failed&&a.Status()!=X86::Application::Exited;++i)a.Pump(8);if(a.Status()==X86::Application::Failed||(a.Status()==X86::Application::Exited&&a.ExitCode())){fprintf(stderr,"FAIL code=%u %s\n",a.ExitCode(),a.Error());assert(false);}}
int main(int argc,char**argv){assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);X86::Application a(log);assert(FixtureModules::KeepNative(a));assert(a.Start(exe.data(),exe.size(),font.data(),font.size(),"TOOLBARINPUT.EXE"));pump(a);unsigned parent=a.MainWindow(),bar=User::GetDlgItem(parent,77);assert(bar);Gdi::POINT at{0,0};User::ScreenToClient(bar,&at);at.x=-at.x;at.y=-at.y;
 auto stage=[&](unsigned n){assert(User::PostMessage(parent,0x401,n,0));pump(a);if(n!=14){char text[32],want[32];User::GetWindowText(parent,text,32);snprintf(want,sizeof want,"stage%02u",n);assert(!strcmp(text,want));}};
 auto mouse=[&](int x,int y,unsigned buttons){a.MouseButtons(at.x+x,at.y+y,buttons);pump(a);};
 auto hash=[&](){unsigned value=0;const auto*p=reinterpret_cast<const unsigned*>(User::Pixels());for(int y=2;y<50;++y)for(int x=0;x<60;++x)value=value*33+p[(at.y+y)*800+at.x+x];return value;};
 unsigned normal=hash();mouse(30,24,0);stage(1);unsigned hot=hash();assert(normal!=hot);mouse(30,24,1);stage(2);unsigned pressed=hash();assert(pressed!=hot);mouse(420,100,1);stage(3);mouse(30,24,1);stage(4);mouse(30,24,0);stage(5);
 mouse(90,24,1);mouse(90,24,0);stage(6);mouse(124,24,1);mouse(124,24,0);stage(7);mouse(158,24,1);mouse(158,24,0);stage(8);mouse(218,24,1);mouse(218,24,0);stage(9);mouse(30,24,0);stage(10);
 mouse(30,24,1);stage(11);mouse(31,24,1);stage(12);mouse(31,24,0);mouse(34,24,1);stage(13);mouse(34,24,0);stage(14);assert(a.Status()==X86::Application::Exited&&!a.ExitCode());printf("TOOLBAR INPUT PASS calls=%u normal=%08x hot=%08x pressed=%08x\n",a.ApiCalls(),normal,hot,pressed);
}
