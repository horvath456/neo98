// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <memory>
#include <cstdio>
#include <cstring>
#include <vector>
#define private public
#include "../src/x86_app.h"
#undef private
#include "fixture_native_modules.h"
static std::vector<unsigned char>load(const char*path){FILE*f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
static void capture(const char*path){User::Render();FILE*f=fopen(path,"wb");assert(f);fprintf(f,"P6\n800 600\n255\n");auto*p=User::Pixels();for(unsigned i=0;i<800*600;++i){unsigned char c[]={p[i*4+2],p[i*4+1],p[i*4]};fwrite(c,1,3,f);}fclose(f);}
static unsigned window(){unsigned h[192],n=0;User::WindowSnapshot(User::GetDesktopWindow(),h,192,n);for(unsigned i=0;i<n;++i){char c[64];User::GetClassName(h[i],c,64);if(!strcmp(c,"Shell660"))return h[i];}return 0;}
static void pump(X86::Application&a){for(unsigned i=0;i<2000;++i){a.Pump(16);if(a.Status()==X86::Application::Failed||a.Status()==X86::Application::Exited||a.WaitingForInput())return;}}
int main(int argc,char**argv){assert(argc==6);auto exe=load(argv[1]),font=load(argv[2]),dll=load(argv[3]);assert(!Registry::System().nodes[0].deleted);unsigned large=0,small=0;
 for(unsigned boundary=0;boundary<2;++boundary){auto a_owner=std::make_unique<X86::Application>(nullptr);auto &a=*a_owner;assert(FixtureModules::KeepNative(a));assert(a.Start(exe.data(),exe.size(),font.data(),font.size(),"SHELL660.EXE"));a.threads[0].suspended=1;
  if(boundary){const unsigned char cache[]={1,2,3};assert(!a.files.store.Seed("C:\\WINDOWS\\ShellIconCache",cache,sizeof cache));}
  const unsigned args[]={0};a.ShellIconCacheApi(660,args);assert(a.Status()==X86::Application::Failed);assert(strstr(a.Error(),boundary?"Existing ShellIconCache":"needs C:"));for(unsigned i=0;i<16;++i)assert(!a.SharedImageLists()[i].used);
  if(boundary)assert(!a.files.store.Remove("C:\\WINDOWS\\ShellIconCache",false));printf("SHELL660 provider boundary PASS %u\n",boundary);
 }

 for(unsigned pass=0;pass<2;++pass){auto a_owner=std::make_unique<X86::Application>(nullptr);auto &a=*a_owner;assert(FixtureModules::KeepNative(a));assert(!a.files.store.Seed("C:\\WINDOWS\\SYSTEM\\SHELL32.DLL",dll.data(),dll.size()));assert(a.Start(exe.data(),exe.size(),font.data(),font.size(),"SHELL660.EXE",pass?"2":"1"));pump(a);
  if(!pass){if(a.Status()==X86::Application::Failed||a.Status()==X86::Application::Exited)fprintf(stderr,"SHELL660 initial status=%u exit=%u error=%s\n",a.Status(),a.ExitCode(),a.Error());assert(a.Status()!=X86::Application::Failed&&a.Status()!=X86::Application::Exited);unsigned h=window();assert(h);capture(argv[4]);auto*l=a.SharedImageLists();assert(l[0].count==45&&l[1].count==45);for(unsigned i=0;i<45;++i){assert(l[0].items[i]&&l[1].items[i]);assert(l[0].items[i]->width==32&&l[1].items[i]->width==16);}large=0x60000000|(l[0].generation<<8)|1;small=0x60000000|(l[1].generation<<8)|2;User::PostMessage(h,0x8001,0,0);pump(a);assert(a.Status()!=X86::Application::Failed&&a.Status()!=X86::Application::Exited);capture(argv[5]);assert(l[0].width==40&&l[1].width==20&&l[0].count==45&&l[1].count==45);User::PostMessage(h,0x10,0,0);pump(a);}
  if(a.Status()!=X86::Application::Exited||a.ExitCode())fprintf(stderr,"SHELL660 status=%u exit=%u error=%s\n",a.Status(),a.ExitCode(),a.Error());assert(a.Status()==X86::Application::Exited&&!a.ExitCode());assert(a.FindImageList(large)&&a.FindImageList(small));for(unsigned index=0;index<1023;++index){const auto&i=X86::Application::SharedIcons()[index];assert(i.owner!=&a||!i.data||i.shared);}printf("SHELL660 NATIVE PASS calls=%u callbacks=%u pass=%u\n",a.ApiCalls(),a.Callbacks(),pass);
 }assert(!User::LiveWindows()&&!Gdi::LiveObjects());}
