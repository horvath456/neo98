// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include <vector>
#include <cstring>
#define private public
#include "../src/x86_app.h"
#include "fixture_native_modules.h"
#undef private
static std::vector<unsigned char>load(const char*p){FILE*f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);auto n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char**argv){assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);{
 X86::Application app(nullptr);assert(FixtureModules::KeepNative(app));assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"TOOLBAR-LAYOUT.EXE"));for(unsigned n=0;n<10000&&app.Status()!=X86::Application::Failed&&app.Status()!=X86::Application::Exited;++n)app.Pump(32);
 fprintf(stderr,"toolbar state=%u code=%u error=%s calls=%u\n",app.Status(),app.ExitCode(),app.Error(),app.ApiCalls());assert(app.Status()==X86::Application::Exited&&!app.ExitCode());printf("TOOLBAR LAYOUT GUEST PASS calls=%u callbacks=%u\n",app.ApiCalls(),app.Callbacks());
 }
 {
 X86::Application app(nullptr);assert(FixtureModules::KeepNative(app));assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"TOOLBAR-LAYOUT.EXE"));app.threads[0].suspended=1;
 unsigned init=app.Allocate(8);app.Write32(init,8);app.Write32(init+4,4);assert(app.CommonInitApi("InitCommonControlsEx",&init));app.Free(init);
 unsigned name=app.String("ToolbarWindow32"),create[]={0,name,0,0,0,0,100,24,0,0,app.image.Base(),0},h=app.CreateWindow(create);assert(h);app.Free(name);unsigned object=app.toolbars[0].object,old=app.Read32(object+0x2c);assert(old);
 // Creation stops where the object table of the port is full
 // (Gdi::ObjectCapacity), not at a guessed count of 256.
 std::vector<unsigned>bitmaps;unsigned a[]={24,24,1,1,0};const unsigned limit=Gdi::ObjectCapacity();for(unsigned i=0;i<limit;++i){unsigned bitmap=app.GdiApi("CreateBitmap",a);if(!bitmap)break;bitmaps.push_back(bitmap);}assert(!bitmaps.empty()&&bitmaps.size()<limit);
 assert(!app.ToolbarMessage(h,0x41f,0,0x00280032));assert(app.Read32(object+0x2c)==old&&app.ToolbarMessage(h,0x43a,0,0)==0x00160017);assert(app.Status()!=X86::Application::Failed);
 for(unsigned bitmap:bitmaps)assert(app.GdiApi("DeleteObject",&bitmap));assert(app.ToolbarMessage(h,0x41f,0,0x00280032));assert(app.Read32(object+0x2c)!=old);assert(app.Api("DestroyWindow",&h));
 puts("TOOLBAR LAYOUT GDI OOM PASS: actual object exhaustion, unchanged dimensions/backing, free/retry and destruction");
 }
 puts("TOOLBAR LAYOUT NATIVE PASS");}
