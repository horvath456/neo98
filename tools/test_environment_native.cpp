// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include <cstdlib>
// libstdc++ wraps <stdlib.h> with using std::malloc: take it before the
// allocator is renamed below.
#include <stdlib.h>
#include <cstring>
#include <vector>
#define private public
#include "../src/x86_app.h"
#undef private
static bool fail_temporary = false;
static unsigned cleanup_calls = 0, cleanup_base = 0, cleanup_pages = 0, pages_before_cleanup = 0;
static void *EnvironmentAllocate(size_t bytes)
{ if (fail_temporary) { fail_temporary = false; return nullptr; } return malloc(bytes); }
static void EnvironmentObserve(X86::Application *app, bool done)
{
    if (!done)
    {
        cleanup_base = app->process_environment ? app->process_environment - 32 : 0;
        cleanup_pages = cleanup_base ? app->Read32(cleanup_base + 8) / 4096 : 0;
        pages_before_cleanup = app->virtual_memory.CommittedPages();
    }
    else if (cleanup_base)
    {
        VirtualMemory::Info info{};
        assert(!app->process_environment && !app->virtual_memory.Access(cleanup_base, 1, false));
        assert(app->virtual_memory.Query(cleanup_base, info) && info.state == 0x10000);
        assert(app->virtual_memory.CommittedPages() == pages_before_cleanup - cleanup_pages);
        ++cleanup_calls;
    }
}
#define malloc EnvironmentAllocate
#include "../src/x86_environment_test.inc"
#undef malloc
static std::vector<unsigned char> load(const char *name)
{
    FILE *f=fopen(name,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
    std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;
}
int main(int argc,char **argv)
{
    assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);auto *app=new X86::Application(nullptr);
    assert(app->Start(exe.data(),exe.size(),font.data(),font.size(),"ENVIRONMENT.EXE"));
    const unsigned block=app->process_environment,base=block-32;
    assert(block && app->Read32(base)==0x45484350 && app->Read32(base+4)==16384 && app->Read32(base+8)==4096);
    const unsigned old_crt_vector=app->Read32(app->crt_data+32),old_temp=app->FindProcessEnvironment("tEmP");assert(old_crt_vector && old_temp);
    char result[128];assert(app->ExpandProcessEnvironment("%TEMP%\\sample",result,sizeof result)==23 && !strcmp(result,"C:\\WINDOWS\\TEMP\\sample"));
    assert(app->ExpandProcessEnvironment("%TEMP%",nullptr,0)==16);
    const unsigned name=app->String("NATIVE_ENV_GROW");std::vector<char>large(5001,'x');large.back()=0;
    const unsigned value=app->Allocate(large.size());assert(app->Store(value,large.data(),large.size()));
    std::vector<unsigned char>before(4096);assert(app->Read(base,before.data(),before.size()));
    const unsigned pages=app->virtual_memory.CommittedPages();
    // Force the real Unicorn map provider to reject the next commit; the Space
    // still describes that page as reserved. This is not a mocked Set failure.
    assert(app->cpu->Map(base+4096,4096,3)==X86::CpuError::Ok);
    unsigned a[]={name,value,0};assert(!app->EnvironmentApi("SetEnvironmentVariableA",a) && app->last_error==8);
    std::vector<unsigned char>after(4096);assert(app->Read(base,after.data(),after.size()));
    assert(before==after && app->virtual_memory.CommittedPages()==pages && app->process_environment==block);
    assert(app->cpu->Unmap(base+4096,4096)==X86::CpuError::Ok);
    assert(app->EnvironmentApi("SetEnvironmentVariableA",a) && app->process_environment==block && app->Read32(base+8)==8192);
    assert(app->virtual_memory.CommittedPages()==pages+1 && app->Read32(app->crt_data+32)==old_crt_vector);
    a[1]=0;assert(app->EnvironmentApi("SetEnvironmentVariableA",a));
    // CRT's startup vector is a separate original copy. K32 mutations must not
    // silently rewrite its writable pointer slots or initial environment.
    const unsigned tempname=app->String("TEMP"),tempvalue=app->String("C:\\NEW");
    a[0]=tempname;a[1]=tempvalue;assert(app->EnvironmentApi("SetEnvironmentVariableA",a));
    bool found=false;
    for(unsigned p=old_crt_vector,v;(v=app->Read32(p));p+=4)
    { if(!strcmp(app->ReadString(v),"TEMP=C:\\WINDOWS\\TEMP"))found=true; }
    assert(found && app->ExpandProcessEnvironment("%TEMP%",result,sizeof result)==7 && !strcmp(result,"C:\\NEW"));
    a[1]=app->String("C:\\WINDOWS\\TEMP");assert(app->EnvironmentApi("SetEnvironmentVariableA",a));
    char long_name[110];long_name[0]='%';memset(long_name+1,'Q',105);long_name[106]='%';long_name[107]=0;
    fail_temporary=true;app->last_error=0;
    assert(!app->ExpandProcessEnvironment(long_name,result,sizeof result) && !fail_temporary && app->last_error==8);
    assert(app->ExpandProcessEnvironment(long_name,result,sizeof result)==108 && !strcmp(result,long_name) && app->last_error==203);
    const unsigned output=app->Allocate(4096),missing=app->String("MISSING_ENV");
    const unsigned protect[]={output&~4095u,4096,2,0};assert(app->MemoryApi("VirtualProtect",protect));
    a[0]=missing;a[1]=output;a[2]=1;app->last_error=0;
    assert(!app->EnvironmentApi("GetEnvironmentVariableA",a) && app->last_error==87 && app->Status()!=X86::Application::Failed);
    const unsigned restore[]={output&~4095u,4096,4,0};assert(app->MemoryApi("VirtualProtect",restore));
    for(unsigned i=0;i<20000 && app->Status()!=X86::Application::Exited && app->Status()!=X86::Application::Failed;++i)app->Pump();
    if(app->Status()!=X86::Application::Exited || app->ExitCode())fprintf(stderr,"Environment guest state%d code%u error%s\n",app->Status(),app->ExitCode(),app->Error());
    assert(app->Status()==X86::Application::Exited && !app->ExitCode());
    printf("ENVIRONMENT NATIVE PASS %u API calls: actual commit failure/retry, stable block, CRT copy, shared path lookup, host expansion, three workers\n",app->ApiCalls());
    delete app;assert(cleanup_calls==1);puts("ENVIRONMENT TEARDOWN PASS: owned pages released before backend destruction");
}
