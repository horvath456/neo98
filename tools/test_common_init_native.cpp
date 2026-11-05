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
static std::vector<unsigned char> Load(const char *name){
    FILE*f=std::fopen(name,"rb");assert(f);std::fseek(f,0,SEEK_END);
    long n=std::ftell(f);assert(n>0);std::rewind(f);
    std::vector<unsigned char> data(n);assert(std::fread(data.data(),1,n,f)==static_cast<size_t>(n));
    std::fclose(f);return data;
}
int main(int argc,char**argv){
    assert(argc==3);auto exe=Load(argv[1]),font=Load(argv[2]);
    for(unsigned cycle=0;cycle<3;++cycle){
        auto app_storage=std::make_unique<X86::Application>(nullptr);
        auto &app=*app_storage;
        assert(FixtureModules::KeepNative(app));
        assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"COMMONINIT.EXE"));
        for(unsigned n=0;n<10000&&app.Status()!=X86::Application::Exited&&app.Status()!=X86::Application::Failed;++n)app.Pump(32);
        if(app.Status()!=X86::Application::Exited||app.ExitCode())
            std::fprintf(stderr,"common init: state=%u exit=%u error=%s\n",app.Status(),app.ExitCode(),app.Error());
        assert(app.Status()==X86::Application::Exited&&!app.ExitCode());
        std::printf("COMMON INIT GUEST PASS cycle=%u calls=%u callbacks=%u\n",cycle,app.ApiCalls(),app.Callbacks());
    }
    for(unsigned profile=0;profile<2;++profile){
        auto app_storage=std::make_unique<X86::Application>(nullptr);
        auto &app=*app_storage;
        assert(FixtureModules::KeepNative(app));
        assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"COMMONINIT.EXE"));
        app.threads[0].suspended=1;
        if(!profile){
            const unsigned bad[]={1};app.CommonInitApi("InitCommonControlsEx",bad);
            assert(app.Status()==X86::Application::Failed);
        }else{
            const unsigned input=app.Allocate(8);app.Write32(input,8);app.Write32(input+4,0x400);
            const unsigned args[]={input};assert(app.CommonInitApi("InitCommonControlsEx",args));
            const unsigned name=app.String("ReBarWindow32");
            const unsigned create[]={0,name,0,0,0,0,100,30,0,0,app.image.Base(),0};
            app.CreateWindow(create);
            assert(app.Status()==X86::Application::Failed);
            assert(std::strstr(app.Error(),"COMCTL32 control procedure not ported: ReBarWindow32"));
        }
    }
    std::puts("COMMON INIT NATIVE PASS: repeated class lifetime, real controls, invalid guest pointer and explicit missing-control boundary");
}
