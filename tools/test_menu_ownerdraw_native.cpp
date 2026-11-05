// SPDX-License-Identifier: GPL-2.0-or-later
// Owner-draw, bitmap, checked and column menu items through real Application/USER callbacks.
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#define private public
#include "../src/x86_app.h"
#undef private
#include "../src/user.cpp"
static std::vector<unsigned char> load(const char*p){FILE*f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);auto n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
static void capture(const char*path){User::Render();FILE*f=fopen(path,"wb");assert(f);fprintf(f,"P6\n800 600\n255\n");auto*p=User::Pixels();for(unsigned n=0;n<800*600;++n){unsigned char rgb[3]={p[n*4+2],p[n*4+1],p[n*4]};assert(fwrite(rgb,1,3,f)==3);}fclose(f);}
static unsigned pixel(int x,int y){auto*p=User::Pixels();return (unsigned(p[(y*800+x)*4+2])<<16)|(unsigned(p[(y*800+x)*4+1])<<8)|p[(y*800+x)*4];}
int main(int argc,char**argv)
{
 assert(argc==4);
 auto exe=load(argv[1]),font=load(argv[2]);
 X86::Application app(nullptr);
 assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"MENUODRW.EXE"));
 unsigned stage=0,inputs=0,columns=0,owner_paint=0;
 for(unsigned n=0;n<10000&&app.Status()!=X86::Application::Failed&&app.Status()!=X86::Application::Exited;++n)
 {
  app.Pump(16);
  const unsigned callbacks=app.Callbacks();
  User::dirty.Mark();User::Render();
  assert(app.Callbacks()==callbacks); // Composition never enters guest code.
  if(!User::PopupActive())continue;
  if(stage>=2)continue;
  auto*view=User::PopupTop();assert(view);
  const unsigned menu=view->popup.menu,owner=view->popup.owner;
  auto*object=User::FindMenu(menu);assert(object&&object->count==7);
  if(stage==0)
  {
   if(!view->pixels)continue; // The yielding first draw has not published yet.
   // The implemented layout must place the break item in a second column.
   assert(object->items[5].x>object->items[0].x);
   assert(object->items[5].y==object->items[0].y);
   ++columns;
   // Separator, owner-draw and bitmap sizes come from the original measurement.
   assert(object->items[1].height==User::SystemSettings().Word(0x34c)/2);
   assert(object->items[2].height==24&&object->items[2].width>=90);
   assert(object->items[2].bitmap_width==90&&object->items[2].bitmap_height==24);
   assert(object->items[3].bitmap_width==20&&object->items[3].bitmap_height==12);
   assert(object->scroll>=13); // check/bitmap column
   User::Render();
   Gdi::RECT rect{};
   // GetMenuItemRect measures against the window passed; only the popup
   // window (#32768) gives screen positions inside the menu (menu_item_rect
   // contract on Win98 SE).
   assert(User::MenuItemRect(view->popup.window,menu,2,&rect));
   assert(User::MenuItemFromPoint(owner,menu,(rect.left+rect.right)/2,(rect.top+rect.bottom)/2)==2);
   // The owner painted its item blue through the shared surface.
   if(pixel((rect.left+rect.right)/2,(rect.top+rect.bottom)/2)==0)++owner_paint;
   capture(argv[3]);
   User::PostMessage(owner,0x8001,0,0);
   app.Pump(1);
   if(app.Status()==X86::Application::Failed||app.Status()==X86::Application::Exited)
    fprintf(stderr,"owner-draw probe ended the guest: state=%u code=%u error=%s\n",app.Status(),app.ExitCode(),app.Error());
   assert(app.Status()!=X86::Application::Failed&&app.Status()!=X86::Application::Exited);
   stage=1;
  }
  else if(stage==1)
  {
   Gdi::RECT rect{};
   assert(User::MenuItemRect(view->popup.window,menu,6,&rect));
   const int x=(rect.left+rect.right)/2,y=(rect.top+rect.bottom)/2;
   assert(User::MenuItemFromPoint(owner,menu,x,y)==6);
   app.Mouse(x,y,true,false);app.Mouse(x,y,false,true);
   inputs+=2;stage=2;
  }
 }
 if(app.Status()!=X86::Application::Exited||app.ExitCode())
  fprintf(stderr,"owner-draw menu state=%u code=%u stage=%u error=%s\n",app.Status(),app.ExitCode(),stage,app.Error());
 assert(app.Status()==X86::Application::Exited&&!app.ExitCode());
 if(!(stage==2&&columns==1&&owner_paint==1))fprintf(stderr,"stage=%u columns=%u owner_paint=%u\n",stage,columns,owner_paint);
 assert(stage==2&&columns==1&&owner_paint==1);
 assert(!User::popup_depth);
 for(const auto&v:User::popup_views)assert(!v.used&&!v.pixels);
 for(const auto&q:User::popup_inputs)assert(!q.head&&!q.tail);
 printf("MENU OWNERDRAW NATIVE PASS: calls=%u callbacks=%u input_actions=%u\n",app.ApiCalls(),app.Callbacks(),inputs);
}
