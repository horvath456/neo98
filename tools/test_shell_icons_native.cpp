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
static std::vector<unsigned char> load(const char *path) {
    FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);const long size=ftell(f);rewind(f);
    std::vector<unsigned char> data(size);assert(fread(data.data(),1,size,f)==size_t(size));fclose(f);return data;
}
static unsigned owned(const X86::Application &app) {
    unsigned count=0;const auto *icons=X86::Application::SharedIcons();
    for(unsigned i=0;i<1023;++i)
        count+=icons[i].owner==&app && icons[i].data && !icons[i].shared;
    return count;
}
int main(int argc,char **argv) {
    assert(argc==4);auto exe=load(argv[1]),font=load(argv[2]),pe=load(argv[3]);
    {
        auto app_owner=std::make_unique<X86::Application>(nullptr);auto &app=*app_owner;assert(FixtureModules::KeepNative(app));assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"SHELLICONS.EXE"));
        for(unsigned i=0;i<10000 && app.Status()!=X86::Application::Exited && app.Status()!=X86::Application::Failed;++i)app.Pump(32);
        if(app.Status()!=X86::Application::Exited || app.ExitCode())fprintf(stderr,"shell icons: state=%u code=%u error=%s\n",app.Status(),app.ExitCode(),app.Error());
        assert(app.Status()==X86::Application::Exited && !app.ExitCode());assert(!owned(app));
        printf("SHELL ICONS GUEST PASS: calls=%u callbacks=%u owned_icons=0\n",app.ApiCalls(),app.Callbacks());
    }
    for(unsigned profile=0;profile<6;++profile) {
        auto app_owner=std::make_unique<X86::Application>(nullptr);auto &app=*app_owner;assert(FixtureModules::KeepNative(app));assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"SHELLICONS.EXE"));app.threads[0].suspended=1;
        auto file=pe;
        if(profile==1) { unsigned offset=unsigned(file.size()-1);memcpy(file.data()+0x3c,&offset,4); }
        assert(!app.files.store.Seed("C:\\SHELL-HOST.BIN",file.data(),file.size()));
        const unsigned path=app.String("C:\\SHELL-HOST.BIN");
        if(profile<2) {
            const unsigned a[]={0,path,0};const unsigned icon=app.ShellIconApi("ExtractIconA",a);
            if(profile==0) { assert(icon && owned(app)==1);const unsigned destroy[]={icon};assert(app.ImageApi("DestroyIcon",destroy)); }
            else assert(!icon && !owned(app));
            assert(app.Status()!=X86::Application::Failed);
        }else if(profile==2) {
            const unsigned a[]={path,0,1,0,1};assert(!app.ShellIconApi("ExtractIconExA",a));assert(app.Status()==X86::Application::Failed && !owned(app));
        }else if(profile==3) {
            const unsigned output=app.Allocate(8);app.Write32(output,0x98123456);app.Write32(output+4,0xabcdef98);
            const unsigned a[]={1,0,output,0,2};assert(!app.ShellIconApi("ExtractIconExA",a));
            assert(app.Status()==X86::Application::Failed && !owned(app));
            // Original output clear precedes the first filename access.
            assert(app.Read32(output)==0 && app.Read32(output+4)==0xabcdef98);
        }else if(profile==4) {
            const unsigned a[]={path,0,path,0,2};assert(app.ShellIconApi("ExtractIconExA",a)==~0u);
            assert(app.Status()!=X86::Application::Failed && !owned(app) && app.Read32(path)==0);
        }else {
            const unsigned output=app.Allocate(12);app.Write32(output+8,0xabcdef98);
            const unsigned a[]={path,0,output,output,2};assert(app.ShellIconApi("ExtractIconExA",a)==2);
            assert(app.Read32(output) && app.Read32(output+4) && app.Read32(output+8)==0xabcdef98 && owned(app)==4);
            // Both original size passes allocate images even when the caller
            // aliases their output arrays. The second pass replaces the first.
            const unsigned first=app.Read32(output),second=app.Read32(output+4);
            const unsigned d1[]={first},d2[]={second};assert(app.ImageApi("DestroyIcon",d1));assert(app.ImageApi("DestroyIcon",d2));
            assert(owned(app)==2); // Application teardown owns the overwritten pair.
        }
        // The extractor closed its file lease on both success and failure.
        assert(!app.files.store.Remove("C:\\SHELL-HOST.BIN",false));
    }
    puts("SHELL ICONS NATIVE PASS: original PE/NE/ICO guests, owned pixels, source deletion, repeated handles and fault cleanup");
}
