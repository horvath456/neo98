// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include "../src/user_settings.h"
#include <cassert>
#include <cstdio>
#include <vector>
static void log(const char*p){puts(p);}
static std::vector<unsigned char>load(const char*path){FILE*f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char**argv){assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);auto*a=new X86::Application(log);assert(a->Start(exe.data(),exe.size(),font.data(),font.size(),"COORDINATES.EXE"));
 User::BindTask(0,0);assert(User::RegisterClass("GenerationCoordinate",0,0,0));for(unsigned i=0;i<260;++i){auto h=User::CreateWindow("GenerationCoordinate","",0,32760,32760,100,80,0,0,0,0);assert(h);Gdi::RECT r;assert(User::GetWindowRect(h,&r)&&r.right==32860&&r.bottom==32840);assert(User::DestroyWindow(h));}
 // Occupy a second native slot so the guest receives the repeatedly reused one.
 assert(User::RegisterClass("LegacyCoordinate",0,0,0));auto temporary=User::CreateWindow("GenerationCoordinate","",0,0,0,1,1,0,0,0,0);auto legacy=User::CreateWindow("LegacyCoordinate","",0x20000000,0,0,91,79,0,0,0,0);assert(temporary&&legacy&&User::DestroyWindow(temporary));
 auto&settings=User::SystemSettings();settings.SetWord(0x2de,901);settings.SetWord(0x2e0,700);settings.SetWord(0x350,160);settings.SetWord(0x352,24);
 for(unsigned i=0;i<200&&a->Status()!=X86::Application::Failed&&a->Status()!=X86::Application::Exited;++i)a->Pump(8);
 bool okay=a->Status()==X86::Application::Exited&&!a->ExitCode();printf("COORDINATES %s state=%u code=%u calls=%u callbacks=%u error=%s\n",okay?"PASS":"FAIL",a->Status(),a->ExitCode(),a->ApiCalls(),a->Callbacks(),a->Error());assert(User::DestroyWindow(legacy));delete a;assert(!User::LiveWindows());return okay?0:1;
}
