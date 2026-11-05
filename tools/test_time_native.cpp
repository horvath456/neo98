// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include <vector>
#define private public
#include "../src/x86_app.h"
#undef private
static std::vector<unsigned char> load(const char *path)
{
    FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
    std::vector<unsigned char> b(n);assert(fread(b.data(),1,n,f)==(size_t)n);fclose(f);return b;
}
int main(int argc,char **argv)
{
    assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);
    auto *app=new X86::Application(nullptr);
    assert(app->Start(exe.data(),exe.size(),font.data(),font.size(),"TIME.EXE"));
    const unsigned p=app->Allocate(32),a[]={p,p+8};assert(p);
    for(int bias: {0,60,-330})
    {
        app->ConfigureTimeZoneBias(bias);
        app->Write32(p,0);app->Write32(p+4,0);app->last_error=98;
        assert(app->TimeApi("FileTimeToLocalFileTime",a)==1 && app->last_error==98);
        const unsigned long long expected=0ull-(unsigned long long)((long long)bias*600000000);
        assert(app->Read32(p+8)==unsigned(expected) && app->Read32(p+12)==unsigned(expected>>32));
        const unsigned back[]={p+8,p+8};
        assert(app->TimeApi("LocalFileTimeToFileTime",back)==1 && !app->Read32(p+8) && !app->Read32(p+12));
        const unsigned dos[]={0x21,0,p};
        assert(app->TimeApi("DosDateTimeToFileTime",dos)==1);
        assert(app->Read32(p)==0xe1d58000 && app->Read32(p+4)==0x01a8e79f);
        const unsigned packed[]={p,p+16,p+18};
        assert(app->TimeApi("FileTimeToDosDateTime",packed)==1 && app->Read32(p+16)==0x21);
    }
    // time_family_fixture.c carries the constants of the machine's own bias,
    // which KERNEL32 takes from the hive: since the ISO installation (2eb7f05)
    // Pacific, ActiveTimeBias 420. State it here instead of relying
    // on the loaded default, so this test keeps working without a registry
    // image; the fixture and this line change together.
    app->ConfigureTimeZoneBias(420);
    for(unsigned i=0;i<10000 && app->Status()!=X86::Application::Exited && app->Status()!=X86::Application::Failed;++i)app->Pump();
    if(app->Status()!=X86::Application::Exited || app->ExitCode())
        fprintf(stderr,"Time guest state%d code%u error%s\n",app->Status(),app->ExitCode(),app->Error());
    assert(app->Status()==X86::Application::Exited && !app->ExitCode());
    printf("TIME NATIVE PASS: calls=%u; three configured bias profiles and x86 fixture\n",app->ApiCalls());
    delete app;
}
