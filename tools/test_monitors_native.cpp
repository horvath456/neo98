// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include "../src/user_settings.h"
#include <cassert>
#include <cstdio>
#include <vector>
static void log(const char*p){puts(p);}
static std::vector<unsigned char>load(const char*path){FILE*f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char**argv){assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);
 auto*a=new X86::Application(log);assert(a->Start(exe.data(),exe.size(),font.data(),font.size(),"MONITORS.EXE"));
 // CreateDC's device-name form first reads [devices] from WIN.INI. Without
 // that file this RAM-only fixture gets ERROR_FILE_NOT_FOUND before GDI
 // runs, invalidating its assertions that an unknown driver preserves the
 // caller's LastError. An empty device list is the intended environment.
 auto &files=Files::System();const auto directory=files.MakeDirectory("C:\\WINDOWS");assert(!directory||directory==183);
 const char ini[]="[devices]\r\n";assert(!files.Seed("C:\\WINDOWS\\WIN.INI",ini,sizeof ini-1));
 User::BindTask(0,0);
 auto&settings=User::SystemSettings();
 // A 1024x768 framebuffer whose work area still reports the 800x600 SPI value,
 // so the monitor rectangle and the work rectangle cannot be confused.
 settings.SetWord(0x2de,1024);settings.SetWord(0x2e0,768);
 settings.SetWord(0x376,0);settings.SetWord(0x378,0);settings.SetWord(0x37a,1024);settings.SetWord(0x37c,768);
 settings.SetWord(0x37e,1);settings.SetWord(0x380,1);
 // Burn through window generations so the guest works with a full-width HWND,
 // which the original 16-bit thunk could not carry.
 assert(User::RegisterClass("GenerationMonitor",0,0,0));
 for(unsigned i=0;i<260;++i){auto g=User::CreateWindow("GenerationMonitor","",0,0,0,10,10,0,0,0,0);assert(g);assert(User::DestroyWindow(g));}
 for(unsigned i=0;i<200&&a->Status()!=X86::Application::Failed&&a->Status()!=X86::Application::Exited;++i)a->Pump(8);
 bool okay=a->Status()==X86::Application::Exited&&!a->ExitCode();
 printf("MONITORS %s state=%u code=%u calls=%u callbacks=%u error=%s\n",okay?"PASS":"FAIL",a->Status(),a->ExitCode(),a->ApiCalls(),a->Callbacks(),a->Error());
 delete a;assert(!User::LiveWindows());return okay?0:1;
}
