// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
static std::vector<unsigned char>load(const char*p){FILE*f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char**argv){assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);for(unsigned round=0;round<3;++round){unsigned before=Gdi::LiveObjects();{X86::Application app(nullptr);assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"CAPTURE.EXE"));unsigned stage=0,hwnd=0;for(unsigned i=0;i<20000&&app.Status()!=X86::Application::Exited&&app.Status()!=X86::Application::Failed;++i){app.Pump(32);if(!stage&&(hwnd=User::FindWindow("MouseCapture98","capture mouse ready"))){stage=1;/* Start over b, in the capture queue; drag onto the desktop while held. */
app.MouseButtons(350,250,0);app.MouseButtons(350,250,1);app.MouseButtons(650,450,1);app.MouseButtons(650,450,0);}if(stage==1&&User::FindWindow("MouseCapture98","capture mouse passed")){stage=2;User::PostMessage(hwnd,0x8010,0,0);}}
 if(app.Status()!=X86::Application::Exited||app.ExitCode())fprintf(stderr,"CAPTURE status=%u code=%u error=%s stage=%u capture=%u\n",app.Status(),app.ExitCode(),app.Error(),stage,User::CaptureOperation(0));assert(app.Status()==X86::Application::Exited&&!app.ExitCode()&&stage==2);printf("Mouse capture guest PASS round=%u calls=%u callbacks=%u mouse=move/down/up capture=persistent\n",round,app.ApiCalls(),app.Callbacks());}assert(Gdi::LiveObjects()==before);}return 0;}
