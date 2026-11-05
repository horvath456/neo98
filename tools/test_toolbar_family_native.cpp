// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <memory>
#include <cstdio>
#include <vector>
#include <cstring>
#define private public
#include "../src/x86_app.h"
#include "fixture_native_modules.h"
#undef private
static std::vector<unsigned char>load(const char*p){FILE*f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);auto n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char**argv){assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);{
 auto app_storage=std::make_unique<X86::Application>(nullptr);
        auto &app=*app_storage;assert(FixtureModules::KeepNative(app));assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"TOOLBAR.EXE"));for(unsigned n=0;n<10000&&app.Status()!=X86::Application::Failed&&app.Status()!=X86::Application::Exited;++n)app.Pump(32);
 fprintf(stderr,"toolbar state=%u code=%u error=%s calls=%u\n",app.Status(),app.ExitCode(),app.Error(),app.ApiCalls());assert(app.Status()==X86::Application::Exited&&!app.ExitCode());printf("TOOLBAR FAMILY GUEST PASS calls=%u callbacks=%u\n",app.ApiCalls(),app.Callbacks());
 }{
 auto app_storage=std::make_unique<X86::Application>(nullptr);
        auto &app=*app_storage;assert(FixtureModules::KeepNative(app));assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"TOOLBAR.EXE"));app.threads[0].suspended=1;
 unsigned init=app.Allocate(8);app.Write32(init,8);app.Write32(init+4,4);unsigned init_args[]={init};assert(app.CommonInitApi("InitCommonControlsEx",init_args));app.Free(init);
 unsigned name=app.String("ToolbarWindow32"),create[]={0,name,0,0,0,0,100,24,0,0,app.image.Base(),0};unsigned h=app.CreateWindow(create);assert(h);app.Free(name);
 auto message=[&](unsigned m,unsigned w=0,unsigned l=0){return app.ToolbarMessage(h,m,w,l);};message(0x41e,20);
 unsigned source=app.Allocate(20),data[]={0xffffffff,200,4,22,0xffffffff};app.Store(source,data,20);assert(message(0x443,0,source));
 auto*object=&app.toolbars[0];assert(object->hwnd==h);unsigned old=app.Read32(object->object+0x30);unsigned before[7];app.Read(old,before,28);
 // Consume real guest heap capacity, then every smaller residual extent. There
 // is no allocator fault switch: the next array reallocation must fail.
 std::vector<unsigned>blocks;for(unsigned bytes=1024*1024;bytes>=16;bytes/=2)for(;;){unsigned args[]={0,bytes};unsigned p=app.LocalMemoryApi("LocalAlloc",args);if(!p)break;blocks.push_back(p);}
 assert(!message(0x443,0,source));assert(app.last_error==8&&message(0x418)==1&&app.Read32(object->object+0x30)==old);unsigned after[7];app.Read(old,after,28);assert(!memcmp(before,after,28));
 for(unsigned p:blocks)assert(!app.LocalMemoryApi("LocalFree",&p));assert(message(0x443,0,source)&&message(0x418)==2);
 // Preserve live owned UTF-16 through array relocation and release at teardown.
 unsigned text=app.Allocate(10);unsigned short word[]={'T','e','s','t',0};app.Store(text,word,10);data[1]=201;data[4]=text;app.Store(source,data,20);assert(message(0x443,0,source));app.Free(text);
 assert(message(0x416,0)&&message(0x418)==2);app.Free(source);unsigned owned_object=object->object,owned_buttons=app.Read32(owned_object+0x30);unsigned destroy[]={h};assert(app.Api("DestroyWindow",destroy));for(auto&t:app.toolbars)assert(!t.object);assert(!app.LocalMemoryApi("LocalSize",&owned_object));assert(!app.LocalMemoryApi("LocalSize",&owned_buttons));
 unsigned handles[64];create[1]=app.String("ToolbarWindow32");for(unsigned&i:handles){i=app.CreateWindow(create);assert(i);}assert(!app.CreateWindow(create)&&app.last_error==8&&app.Status()!=X86::Application::Failed);
 for(unsigned&i:handles)assert(app.Api("DestroyWindow",&i));unsigned reused=app.CreateWindow(create);assert(reused&&app.Api("DestroyWindow",&reused));app.Free(create[1]);
 puts("TOOLBAR NATIVE HEAP PASS: actual exhaustion, unchanged failed insertion, free/retry, copied text and teardown");
 }
 {auto app_storage=std::make_unique<X86::Application>(nullptr);
        auto &app=*app_storage;assert(FixtureModules::KeepNative(app));assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"TOOLBAR.EXE"));app.threads[0].suspended=1;unsigned init=app.Allocate(8);app.Write32(init,8);app.Write32(init+4,4);assert(app.CommonInitApi("InitCommonControlsEx",&init));unsigned name=app.String("ToolbarWindow32"),create[]={0,name,0,0,0,0,100,24,0,0,app.image.Base(),0};unsigned h=app.CreateWindow(create);assert(h);
 // Painting is ported since the toolbar drawing family, so WM_PAINT is no
 // longer a boundary. TB_CUSTOMIZE has no ported customization dialog.
 assert(!app.ToolbarMessage(h,0xf,0,0)&&app.Status()!=X86::Application::Failed);
 app.ToolbarMessage(h,0x41b,0,0);assert(app.Status()==X86::Application::Failed&&strstr(app.Error(),"Toolbar message 041b is not ported yet"));}
 puts("TOOLBAR FAMILY NATIVE PASS");}
