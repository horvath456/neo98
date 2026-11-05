// SPDX-License-Identifier: GPL-2.0-or-later
#define private public
#include "../src/x86_app.h"
#undef private
#include <cassert>
#include <chrono>
#include <cstdio>
#include <vector>
static std::vector<unsigned char> load(const char*p){FILE*f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char**argv){
 assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);
 const unsigned char riff[]={'R','I','F','F',30,0,0,0,'W','A','V','E','f','m','t',' ',3,0,0,0,'f','m','t',0,'d','a','t','a',5,0,0,0,'h','e','l','l','o',0};
 assert(!Files::System().Seed("C:\\WINDOWS\\RIFFTEST.WAV",riff,sizeof riff));
 X86::Application app(nullptr);assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"PINTEST.EXE"));
 const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(15);bool sent=false;
 while(app.Status()!=X86::Application::Exited&&app.Status()!=X86::Application::Failed&&std::chrono::steady_clock::now()<limit){
  app.Pump(32);
  if(!sent&&app.threads[0].block==9){
   const auto*p=app.ConsolePixels();assert(p);
   unsigned colors=0;for(unsigned y=0;y<16;++y)for(unsigned x=0;x<128;++x)if(p[y*800+x]!=p[799])++colors;
   assert(colors>100);
   // Console-subsystem games can own graphics without FreeConsole (NFS II).
   const auto text_hash=app.ConsoleFrameHash();
   User::SetExclusiveMode(true);assert(!app.ConsolePixels());
   User::SetExclusiveMode(false);assert(app.ConsolePixels());
   int width=0,height=0,depth=0;User::ScreenMode(width,height,depth);
   assert(User::SetScreenMode(640,480,16));assert(!app.ConsolePixels());
   assert(User::SetScreenMode(width,height,depth));assert(app.ConsolePixels());
   User::BindTask(app.process_id,app.ThreadId());
   const auto window=User::CreateWindow("STATIC","console GUI",0x90000000,10,10,100,80,0,0,0);
   assert(window);User::SetFocus(window);assert(User::WindowProcess(User::GetFocus()));
   assert(!app.ConsolePixels());
   User::DestroyWindow(window);User::SetFocus(0);
   p=app.ConsolePixels();assert(p&&app.ConsoleFrameHash()==text_hash);

   FILE*f=fopen("build/pinball-console/console.ppm","wb");assert(f);fprintf(f,"P6\n800 600\n255\n");
   for(unsigned i=0;i<800*600;++i){unsigned char rgb[]={static_cast<unsigned char>(p[i]>>16),static_cast<unsigned char>(p[i]>>8),static_cast<unsigned char>(p[i])};fwrite(rgb,1,3,f);}fclose(f);
   app.RawKey('O','O',true,0x18,false);app.RawKey('O',0,false,0x18,false);
   app.RawKey('X','X',true,0x2d,false);app.RawKey('X',0,false,0x2d,false);
   app.RawKey(8,8,true,0x0e,false);app.RawKey(8,0,false,0x0e,false);
   app.RawKey('K','K',true,0x25,false);app.RawKey('K',0,false,0x25,false);
   app.RawKey(13,13,true,0x1c,false);app.RawKey(13,0,false,0x1c,false);sent=true;
  }
 }
 printf("PINBALL/CONSOLE state=%u exit=%u sent=%u calls=%u error=%s\n",app.Status(),app.ExitCode(),sent,app.ApiCalls(),app.Error());fflush(stdout);
 assert(sent&&app.Status()==X86::Application::Exited&&app.ExitCode()==0);
 const auto *final=app.ConsolePixels();assert(final);unsigned written=0;
 for(unsigned y=16;y<32;++y)for(unsigned x=0;x<32;++x)written+=final[y*800+x]!=0;
 assert(written>100);
}
