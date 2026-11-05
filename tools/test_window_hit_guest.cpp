// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
static std::vector<unsigned char>load(const char*p){FILE*f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);auto n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char**argv){assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);unsigned before=Gdi::LiveObjects();{X86::Application app(nullptr);assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"POINTHIT.EXE"));bool sent=false;for(unsigned n=0;n<6000&&app.Status()!=X86::Application::Exited&&app.Status()!=X86::Application::Failed;++n){app.Pump(32);if(!sent){unsigned handles[192],count=0;User::WindowSnapshot(0,handles,192,count);for(unsigned j=0;j<count;++j){char text[80];User::GetWindowText(handles[j],text,sizeof text);if(!strcmp(text,"point input ready")){app.MouseButtons(30,40,0);app.MouseButtons(30,40,1);app.MouseButtons(30,40,0);sent=true;break;}}}}if(app.Status()!=X86::Application::Exited||app.ExitCode())fprintf(stderr,"POINT status=%u code=%u error=%s\n",app.Status(),app.ExitCode(),app.Error());assert(app.Status()==X86::Application::Exited&&!app.ExitCode()&&sent);printf("Window hit guest PASS calls=%u callbacks=%u actual_input=%u\n",app.ApiCalls(),app.Callbacks(),sent);}assert(Gdi::LiveObjects()==before);}
