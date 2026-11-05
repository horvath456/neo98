// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <cstring>
static std::vector<unsigned char> Load(const char*name){FILE*f=fopen(name,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>v(n);assert(fread(v.data(),1,n,f)==size_t(n));fclose(f);return v;}
static void RenderScroll(const char*output){
 assert(User::RegisterClass("ScrollPixels",0,0,0));
 unsigned h=User::CreateWindow("ScrollPixels","Original Win98 USER scrollbar state",0x10cf0000,35,40,460,340,0,0,0);
 assert(h);User::CompleteCreation(h);Win98Scroll::Info info{28,7,0,999,150,200,0};
 assert(User::ScrollState(h,1,0,&info,1)==200);info.position=400;assert(User::ScrollState(h,1,1,&info,1)==400);
 assert(User::Render());std::vector<unsigned char>before(User::Pixels(),User::Pixels()+800*600*4);
 unsigned serial=User::FrameSerial();assert(!User::Render()&&User::FrameSerial()==serial);
 Gdi::POINT origin{};assert(User::ScreenToClient(h,&origin));origin.x=-origin.x;origin.y=-origin.y;Gdi::RECT client{};assert(User::GetClientRect(h,&client));
 info.mask=4;info.position=800;assert(User::ScrollState(h,1,0,&info,0)==800);assert(!User::Render());
 assert(User::ScrollState(h,1,0,&info,1)==800&&User::Render());
 unsigned changed=0;for(int y=0;y<600;++y)for(int x=0;x<800;++x){unsigned p=(y*800+x)*4;if(memcmp(before.data()+p,User::Pixels()+p,4)){
  ++changed;assert(x>=origin.x&&x<origin.x+client.right&&y>=origin.y+client.bottom&&y<origin.y+client.bottom+17);
 }}assert(changed>300);
 before.assign(User::Pixels(),User::Pixels()+800*600*4);
 assert(User::ScrollState(h,3,1,nullptr,3)&&User::Render());
 unsigned disabled_changed=0;for(int y=0;y<600;++y)for(int x=0;x<800;++x){unsigned p=(y*800+x)*4;if(memcmp(before.data()+p,User::Pixels()+p,4)){
  ++disabled_changed;assert(x>=origin.x+client.right&&x<origin.x+client.right+17&&y>=origin.y&&y<origin.y+client.bottom);
 }}assert(disabled_changed>150);

 if(output){FILE*f=fopen(output,"wb");assert(f);fprintf(f,"P6\n800 600\n255\n");const auto*p=User::Pixels();for(unsigned i=0;i<800*600;++i){unsigned char rgb[]{p[4*i+2],p[4*i+1],p[4*i]};assert(fwrite(rgb,3,1,f)==1);}fclose(f);}
 assert(User::DestroyWindow(h));
 h=User::CreateWindow("ScrollPixels","draw allocates scrollbar data",0x10ff0000,20,20,140,120,0,0,0);assert(h);User::CompleteCreation(h);
 Win98Scroll::Info query{28,0x17,0,0,0,0,0};assert(!User::ScrollState(h,0,0,&query,0));
 assert(User::Render());assert(User::ScrollState(h,0,0,&query,0)&&query.minimum==0&&query.maximum==100);assert(User::DestroyWindow(h));
 printf("Scrollbar compositor PASS: %u horizontal pixels changed; redraw=0 preserves frame; disabled vertical thumb hidden\n",changed);
}
int main(int argc,char**argv){assert(argc==3||argc==4);auto exe=Load(argv[1]),font=Load(argv[2]);for(unsigned round=0;round<3;++round){const unsigned before=Gdi::LiveObjects();{X86::Application app(nullptr);assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"SCROLL.EXE"));for(unsigned i=0;i<10000&&app.Status()!=X86::Application::Exited&&app.Status()!=X86::Application::Failed;++i)app.Pump(32);if(app.Status()!=X86::Application::Exited||app.ExitCode())fprintf(stderr,"SCROLL guest: status=%u code=%u error=%s\n",app.Status(),app.ExitCode(),app.Error());assert(app.Status()==X86::Application::Exited&&!app.ExitCode());if(!round)RenderScroll(argc==4?argv[3]:nullptr);printf("SCROLL guest PASS round=%u calls=%u callbacks=%u\n",round,app.ApiCalls(),app.Callbacks());}assert(Gdi::LiveObjects()==before);}return 0;}
