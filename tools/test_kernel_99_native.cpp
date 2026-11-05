/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/x86_app.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <thread>
#include <chrono>
static std::vector<unsigned char> load(const char*path){FILE*f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char**argv){assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);for(unsigned pass=0;pass<2;++pass){X86::Application app(nullptr);assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"KERNEL99.EXE",pass?"KERNEL99.EXE !":nullptr));for(unsigned n=0;n<6000&&app.Status()!=X86::Application::Failed&&app.Status()!=X86::Application::Exited;++n){app.Pump(16);std::this_thread::sleep_for(std::chrono::milliseconds(1));}if(app.Status()!=X86::Application::Exited||app.ExitCode())fprintf(stderr,"KERNEL99 failed status=%u code=%u error=%s\n",app.Status(),app.ExitCode(),app.Error());assert(app.Status()==X86::Application::Exited&&app.ExitCode()==0);printf("KERNEL99 NATIVE PASS calls=%u pass=%u\n",app.ApiCalls(),pass);}}
