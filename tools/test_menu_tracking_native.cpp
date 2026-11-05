// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include <vector>
#include <string>
#define private public
#include "../src/x86_app.h"
#undef private
#include "../src/user.cpp"
static std::vector<unsigned char> load(const char*p){FILE*f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);auto n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
static void key(X86::Application&app,unsigned k){app.RawKey(k,0,true);app.RawKey(k,0,false);}
static void capture(const char*path){User::Render();FILE*f=fopen(path,"wb");assert(f);fprintf(f,"P6\n800 600\n255\n");auto*p=User::Pixels();for(unsigned n=0;n<800*600;++n){unsigned char rgb[3]={p[n*4+2],p[n*4+1],p[n*4]};assert(fwrite(rgb,1,3,f)==3);}fclose(f);}
int main(int argc,char**argv){assert(argc==4);auto exe=load(argv[1]),font=load(argv[2]);X86::Application app(nullptr);assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"MENUPOP.EXE"));unsigned stage=0,inputs=0;
 for(unsigned n=0;n<10000&&app.Status()!=X86::Application::Failed&&app.Status()!=X86::Application::Exited;++n){app.Pump(16);
  if(User::PopupActive()){auto*view=User::PopupTop();assert(view);unsigned menu=view->popup.menu;auto owner=view->popup.owner;
   if(stage==0){auto*spare=User::popup_environment.CreatePopup(menu,owner,{10,10},false);assert(spare);auto saved=User::popup_depth;User::popup_depth=8;assert(!User::popup_environment.BeginSession(*spare,1));User::popup_depth=saved;User::popup_environment.DestroyPopup(*spare);assert(!User::PopupForCore(*spare));capture(argv[3]);User::PostMessage(owner,0x8001,0,0);app.Pump(1);assert(app.Status()!=X86::Application::Failed&&app.Status()!=X86::Application::Exited);assert(view->geometry.outer.right<=800&&view->geometry.outer.bottom<=600);Gdi::RECT rect{};assert(User::MenuItemRect(view->popup.window,menu,2,&rect));assert(User::MenuItemFromPoint(owner,menu,(rect.left+rect.right)/2,(rect.top+rect.bottom)/2)==2);key(app,0x28);key(app,0x28);key(app,0xd);stage=1;inputs+=3;}
   else if(stage==1){Gdi::RECT rect{};assert(User::MenuItemRect(view->popup.window,menu,2,&rect));app.Mouse((rect.left+rect.right)/2,(rect.top+rect.bottom)/2,true,false);app.Mouse((rect.left+rect.right)/2,(rect.top+rect.bottom)/2,false,true);stage=2;inputs+=2;}
   else if(stage==2){auto*m=User::FindMenu(menu);assert(m&&m->count==5&&m->items[4].id==60);Gdi::RECT rect{};assert(User::MenuItemRect(view->popup.window,menu,4,&rect));const int x=(rect.left+rect.right)/2,y=(rect.top+rect.bottom)/2;assert(User::MenuItemFromPoint(owner,menu,x,y)==4);app.Mouse(x,y,true,false);app.Mouse(x,y,false,true);stage=3;inputs+=2;}
   else if(stage==3){Gdi::RECT rect{};assert(User::MenuItemRect(view->popup.window,menu,3,&rect));const int x=(rect.left+rect.right)/2,y=(rect.top+rect.bottom)/2;app.Mouse(x,y,true,false);app.Mouse(x,y,false,true);stage=4;inputs+=2;}
   else if(stage==4){capture((std::string(argv[3])+".child.ppm").c_str());assert(view->parent);key(app,0x1b);stage=5;++inputs;}
   else if(stage==5){assert(!view->parent);key(app,0x1b);stage=6;++inputs;}
  }
 }
 if(app.Status()!=X86::Application::Exited||app.ExitCode())fprintf(stderr,"menu tracking state=%u code=%u stage=%u error=%s\n",app.Status(),app.ExitCode(),stage,app.Error());assert(app.Status()==X86::Application::Exited&&!app.ExitCode());assert(!User::popup_depth);for(const auto&v:User::popup_views)assert(!v.used);printf("POPUP NATIVE PASS: calls=%u callbacks=%u input_actions=%u\n",app.ApiCalls(),app.Callbacks(),inputs);
}
