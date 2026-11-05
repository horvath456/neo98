// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
static void require(bool ok,const char*why){if(!ok){fprintf(stderr,"FILE INFO FAILED: %s\n",why);exit(1);}}
static std::vector<unsigned char>load(const char*p){FILE*f=fopen(p,"rb");require(f,"input");fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);require(fread(b.data(),1,n,f)==unsigned(n),"read");fclose(f);return b;}
int main(int argc,char**argv){require(argc==4,"usage: runner FILEINFO.EXE fixedsys.fnt SHELL32.DLL");auto exe=load(argv[1]),font=load(argv[2]);auto shell=load(argv[3]);require(!Files::System().Seed("C:\\WINDOWS\\SYSTEM\\SHELL32.DLL",shell.data(),shell.size()),"shell resources");X86::Application app(nullptr);require(app.Start(exe.data(),exe.size(),font.data(),font.size(),"FILEINFO.EXE"),"start");auto end=std::chrono::steady_clock::now()+std::chrono::seconds(6);while(app.Status()!=X86::Application::Exited&&app.Status()!=X86::Application::Failed&&std::chrono::steady_clock::now()<end)app.Pump();printf("FILE INFO state=%u exit=%u calls=%u\n",app.Status(),app.ExitCode(),app.ApiCalls());require(app.Status()==X86::Application::Exited&&!app.ExitCode(),app.Error());puts("FILE INFO PASS");}
