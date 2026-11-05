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
    std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==(size_t)n);fclose(f);return b;
}
int main(int argc,char**argv)
{
    assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);auto *app=new X86::Application(nullptr);
    assert(app->Start(exe.data(),exe.size(),font.data(),font.size(),"PROBE.EXE"));
    const unsigned allocate[]={0,8192,0x3000,4};const unsigned p=app->MemoryApi("VirtualAlloc",allocate);assert(p);
    unsigned char one=1,two=2;assert(app->Store(p,&one,1) && app->Store(p+4096,&two,1));
    unsigned a[]={p,4097};app->last_error=123;app->pointer_probe_state.read_sum=91;
    assert(!app->PointerProbeApi("IsBadReadPtr",a) && app->pointer_probe_state.read_sum==3 && app->last_error==123);
    const unsigned protect[]={p+4096,4096,2,p+16};assert(app->MemoryApi("VirtualProtect",protect));
    app->pointer_probe_state.write_add=7;app->last_error=123;
    assert(app->PointerProbeApi("IsBadWritePtr",a)==1 && app->last_error==123);
    assert((app->Read32(p)&255)==8 && (app->Read32(p+4096)&255)==2); // first ADD survives last-byte fault
    app->pointer_probe_state.write_add=0;
    const unsigned decommit[]={p+4096,4096,0x4000};assert(app->MemoryApi("VirtualFree",decommit));
    app->pointer_probe_state.read_sum=91;app->last_error=123;
    assert(app->PointerProbeApi("IsBadReadPtr",a)==1 && app->pointer_probe_state.read_sum==91 && app->last_error==123);
    // A backend failure must be caught even when the Space descriptor still permits access.
    assert(app->cpu->Unmap(p,4096)==X86::CpuError::Ok);a[1]=1;
    assert(app->PointerProbeApi("IsBadReadPtr",a)==1 && app->PointerProbeApi("IsBadWritePtr",a)==1 && app->Status()!=X86::Application::Failed);
    assert(app->cpu->Map(p,4096,X86::Prot::Read|X86::Prot::Write)==X86::CpuError::Ok);
    const unsigned release[]={p,0,0x8000};assert(app->MemoryApi("VirtualFree",release));
    for(unsigned i=0;i<10000 && app->Status()!=X86::Application::Exited && app->Status()!=X86::Application::Failed;++i)app->Pump();
    if(app->Status()!=X86::Application::Exited || app->ExitCode())fprintf(stderr,"Pointer guest state%d code%u error%s\n",app->Status(),app->ExitCode(),app->Error());
    assert(app->Status()==X86::Application::Exited && !app->ExitCode());
    printf("POINTER NATIVE PASS: calls=%u; real protections, partial writes, backend fault and x86 fixture\n",app->ApiCalls());delete app;
}
