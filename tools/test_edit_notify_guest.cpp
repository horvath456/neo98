// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
#include <memory>
static std::vector<unsigned char>Load(const char*name){FILE*f=fopen(name,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>v(n);assert(fread(v.data(),1,n,f)==size_t(n));fclose(f);return v;}
static void Finish(X86::Application&app){for(unsigned n=0;n<10000&&app.Status()!=X86::Application::Exited&&app.Status()!=X86::Application::Failed;++n){app.Pump();if(app.Status()==X86::Application::Idle)std::this_thread::sleep_for(std::chrono::milliseconds(1));}if(app.Status()!=X86::Application::Exited||app.ExitCode())fprintf(stderr,"EDIT notify status=%u code=%u error=%s\n",app.Status(),app.ExitCode(),app.Error());assert(app.Status()==X86::Application::Exited&&!app.ExitCode());}
int main(int argc,char**argv){assert(argc==4);auto exe=Load(argv[1]),font=Load(argv[3]);
 for(unsigned round=0;round<3;++round){unsigned before=Gdi::LiveObjects();{auto instance=std::make_unique<X86::Application>(nullptr);auto&app=*instance;assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"EDITNOTIFY.EXE"));Finish(app);printf("EDIT notify guest PASS round=%u calls=%u callbacks=%u\n",round,app.ApiCalls(),app.Callbacks());}assert(Gdi::LiveObjects()==before);}
 auto host=Load(argv[2]);{auto instance=std::make_unique<X86::Application>(nullptr);auto&app=*instance;assert(app.Start(host.data(),host.size(),font.data(),font.size(),"EDITHOST.EXE"));for(unsigned n=0;n<10000&&!app.WaitingForInput();++n)app.Pump();assert(app.WaitingForInput());unsigned parent=User::FindWindow("EditNotifyPort","EDIT notification"),edit=User::GetDlgItem(parent,1152);assert(edit);unsigned before=app.Callbacks();assert(User::ReplaceEditSelection(edit,"a",1,true));assert(app.Callbacks()==before);Finish(app);assert(app.Callbacks()>before);printf("EDIT notify native caller PASS queued outside app fiber; parent callback Sleep resumed safely\n");}return 0;
}
