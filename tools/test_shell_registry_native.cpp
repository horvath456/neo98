// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#define private public
#include "../src/x86_app.h"
#undef private
#include "fixture_native_modules.h"
static unsigned fail_allocation=0, allocation_calls=0;
static unsigned fail_local_allocation=0, last_local_allocation=0;
static unsigned RegistryLocalAllocate(X86::Application &app,unsigned flags,unsigned size)
{
    if(fail_local_allocation && !--fail_local_allocation){app.last_error=8;return 0;}
    const unsigned args[]={flags,size};
    return last_local_allocation=app.Api("LocalAlloc",args);
}
static void *RegistryAllocate(size_t n)
{ ++allocation_calls;if(fail_allocation && !--fail_allocation)return nullptr;return malloc(n); }
#define malloc RegistryAllocate
#include "../src/x86_shell_registry.cpp"
#undef malloc
static std::vector<unsigned char> load(const char *path)
{
    FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);const auto n=ftell(f);rewind(f);
    std::vector<unsigned char> bytes(n);assert(fread(bytes.data(),1,n,f)==size_t(n));fclose(f);return bytes;
}
static unsigned blocks(X86::Application &a){unsigned n=0;for(auto &b:a.AllocationBlocks())n+=b.used;return n;}
static unsigned handles(X86::Application &a){unsigned n=0;for(unsigned i=0;i<a.registry.handle_capacity;++i)n+=a.registry.handles[i].used;return n;}
int main(int argc,char **argv)
{
    assert(argc==3);const auto exe=load(argv[1]),font=load(argv[2]);auto *app=new X86::Application(nullptr);
    assert(FixtureModules::KeepNative(*app));
    assert(app->Start(exe.data(),exe.size(),font.data(),font.size(),"SHELLREG.EXE"));
    const unsigned path=app->String("Software\\NEO98\\NativeShellRegistry"),name=app->String("Expanded"),value=app->String("%TMP%\\shell");
    const unsigned memory=app->Allocate(16384),out=memory+16,type=memory+8,size=memory+12;
    unsigned a[]={0x80000001,path,name,2,value,12,0,0};
    assert(!app->ShellRegistryApi("SHSetValueA",a));
    const unsigned initial_blocks=blocks(*app),initial_handles=handles(*app);
    a[0]=path;a[1]=3;a[2]=0;a[3]=memory;a[4]=0;
    for(unsigned n=0;n<160;++n)
    {
        assert(!app->ShellRegistryApi("SHRegOpenUSKeyA",a));
        const unsigned key=app->Read32(memory);assert(key && app->LocalMemoryApi("LocalSize",&key)>=276);
        assert(handles(*app)==initial_handles+1);
        const unsigned close[]={key};assert(!app->ShellRegistryApi("SHRegCloseUSKey",close));
        assert(!app->LocalMemoryApi("LocalSize",&key));
        assert(blocks(*app)==initial_blocks && handles(*app)==initial_handles);
    }
    // Host name allocation failure returns through the original Open cleanup.
    fail_allocation=1;app->Write32(memory,123);
    assert(app->ShellRegistryApi("SHRegOpenUSKeyA",a)==8 && !fail_allocation && !app->Read32(memory));
    assert(blocks(*app)==initial_blocks && handles(*app)==initial_handles);
    assert(!app->LocalMemoryApi("LocalSize",&last_local_allocation));
    // Handles grow beyond the old 128-slot table. Exhaust the token range
    // directly so failure cleanup does not require millions of live keys.
    std::vector<unsigned> open;bool created=false;
    for(unsigned n=0;n<160;++n){unsigned h=0;assert(!app->registry.Open(0x80000001,"Software\\NEO98",false,h,created));open.push_back(h);}
    assert(handles(*app)==initial_handles+160);
    const unsigned next_handle=app->registry.next_handle;
    app->registry.next_handle=0x08000000;
    assert(app->ShellRegistryApi("SHRegOpenUSKeyA",a)==8 && !app->Read32(memory));
    assert(!app->LocalMemoryApi("LocalSize",&last_local_allocation));
    assert(blocks(*app)==initial_blocks && handles(*app)==initial_handles+160);
    app->registry.next_handle=next_handle;
    for(unsigned h:open)assert(!app->registry.Close(h));
    assert(!app->ShellRegistryApi("SHRegOpenUSKeyA",a));const unsigned key=app->Read32(memory);
    // Fail the provider's LocalAlloc call, now served by the process heap.
    // Exhausting the separate temporary arena cannot exercise this path.
    fail_local_allocation=1;
    app->Write32(type,0x12345678);app->Write32(size,8192);
    unsigned query[]={key,name,type,out,size,0,0,0};
    assert(app->ShellRegistryApi("SHRegQueryUSValueA",query)==87 && !fail_local_allocation); // LM deferred side is absent after CU's allocation failure.
    // Direct helper exposes ERROR_OUTOFMEMORY(14) and preserves caller metadata.
    const unsigned raw=app->Read32(key);unsigned direct[]={raw,name,0,type,out,size};
    app->Write32(type,0x12345678);app->Write32(size,8192);
    fail_local_allocation=1;
    assert(app->ShellRegistryApi("SHQueryValueExA",direct)==14 && !fail_local_allocation && app->Read32(type)==0x12345678 && app->Read32(size)==8192);
    assert(!app->ShellRegistryApi("SHQueryValueExA",direct) && app->Read32(type)==1 && app->Read32(size)==22 && !strcmp(app->ReadString(out),"C:\\WINDOWS\\TEMP\\shell"));
    assert(!app->LocalMemoryApi("LocalSize",&last_local_allocation));
    // Provider allocation failure and the span limit are checked before host
    // buffers are touched. Retry exercises the same original expansion path.
    X86::Application::ShellRegistryEnvironment environment(*app);
    fail_allocation=1;assert(!environment.Expand(value,out,128) && app->last_error==8 && !fail_allocation);
    const unsigned before=allocation_calls;assert(!environment.Expand(value,out,0x80000000) && app->last_error==87 && allocation_calls==before);
    assert(environment.Expand(value,out,128)==22 && !strcmp(app->ReadString(out),"C:\\WINDOWS\\TEMP\\shell"));
    const unsigned close[]={key};assert(!app->ShellRegistryApi("SHRegCloseUSKey",close));
    assert(!app->LocalMemoryApi("LocalSize",&key));
    assert(blocks(*app)==initial_blocks && handles(*app)==initial_handles);
    unsigned remove[]={0x80000001,path,name};assert(!app->ShellRegistryApi("SHDeleteValueA",remove));
    assert(!app->ShellRegistryApi("SHDeleteEmptyKeyA",remove));
    for(unsigned n=0;n<20000 && app->Status()!=X86::Application::Exited && app->Status()!=X86::Application::Failed;++n)app->Pump();
    if(app->Status()!=X86::Application::Exited||app->ExitCode())fprintf(stderr,"Shell registry state%d code%u error%s\n",app->Status(),app->ExitCode(),app->Error());
    assert(app->Status()==X86::Application::Exited && !app->ExitCode());
    assert(handles(*app)==initial_handles);
    printf("SHELL REGISTRY NATIVE PASS %u x86 API calls: CU/LM views, metadata, expansion, 160 close/reuse cycles, handle/guest/host allocation failures\n",app->ApiCalls());
    delete app;
}
