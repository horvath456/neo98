// SPDX-License-Identifier: GPL-2.0-or-later
// Real x86 workers enter the ordinary public API through existing import gates.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#define private public
#include "../src/x86_app.h"
#include "fixture_native_modules.h"
#undef private
// This TU owns the actual adapter, allowing read-only state inspection without
// adding any production test interface. The build omits its separate object.
#include "../src/x86_kernel_paths.cpp"
using U=unsigned;
constexpr U PathMutex=0x64000004,TaskMutex=0x6400000c;
// The buffers KernelPathEnvironment::Initialize reserves: path, current_scratch,
// set_scratch, temp_scratch, task_directory, drive_directory and the text of the
// current directory.
constexpr U ProviderBuffers=7;
static std::vector<unsigned char> load(const char *name)
{
    FILE *f=fopen(name,"rb");assert(f);fseek(f,0,SEEK_END);const auto n=ftell(f);rewind(f);
    std::vector<unsigned char> data(n);assert(fread(data.data(),1,n,f)==size_t(n));fclose(f);return data;
}
static void main_thread(X86::Application &app)
{
    app.current_thread=0;assert(app.cpu->Restore(app.threads[0].context)==X86::CpuError::Ok);
    app.pending=app.threads[0].pending;app.waiting=app.threads[0].waiting;
    app.depth=app.threads[0].depth;app.last_error=app.threads[0].last_error;
}
static U gate(X86::Application &app,const char *name)
{
    for(U i=0;i<app.import_count;++i)if(!strcmp(app.imports[i].name,name))return 0x70000000+i*16;
    fprintf(stderr,"Missing existing COMMON import: %s\n",name);assert(false);return 0;
}
static U code(X86::Application &app,U address,bool set)
{
    std::vector<unsigned char> b;
    auto byte=[&](U v){b.push_back(v);};auto word=[&](U v){for(U i=0;i<4;++i)byte(v>>(i*8));};
    byte(0x53);byte(0x8b);byte(0x5c);byte(0x24);byte(8); // preserve ebx; report argument
    byte(0xc7);byte(3);word(1); // stage=entered
    if(!set)
    {
        byte(0x8d);byte(0x43);byte(20);byte(0x50); // &report.part
        byte(0xff);byte(0x73);byte(16); // output
        byte(0xff);byte(0x73);byte(12); // capacity
    }
    byte(0xff);byte(0x73);byte(8); // input
    byte(0xb8);word(gate(app,set?"SetCurrentDirectoryA":"GetFullPathNameA"));byte(0xff);byte(0xd0);
    byte(0x89);byte(0x43);byte(4); // result
    byte(0xc7);byte(3);word(2); // stage=returned
    byte(0x5b);byte(0xc2);byte(4);byte(0);
    assert(app.Map(address,4096,7) && app.Store(address,b.data(),b.size()));return address;
}
static U report(X86::Application &app,const char *input)
{
    U name=app.Allocate(strlen(input)+1),out=app.Allocate(260),p=app.Allocate(24);
    const U values[]={0,0,name,260,out,0};assert(name && out && p);
    assert(app.Store(name,input,strlen(input)+1) && app.Store(p,values,sizeof values));
    unsigned char fill[260];memset(fill,0xa5,sizeof fill);assert(app.Store(out,fill,sizeof fill));return p;
}
static U spawn(X86::Application &app,U function,U p)
{
    main_thread(app);const U a[]={0,0,function,p,0,0};const U h=app.ThreadApi("CreateThread",a);assert(h);return h;
}
static U index(X86::Application &app,U handle)
{const auto *h=app.FindKernelHandle(handle);assert(h && h->kind==2);return h->object;}
static void pump(X86::Application &app)
{
    app.Pump(16);
    if(app.Status()==X86::Application::Failed)fprintf(stderr,"Path thread failed: %s\n",app.Error());
    assert(app.Status()!=X86::Application::Failed && app.Status()!=X86::Application::Exited);
}
static void entered(X86::Application &app,const U *p,U count)
{
    for(U n=0;n<100;++n)
    {
        bool all=true;for(U i=0;i<count;++i)all&=app.Read32(p[i])==1;
        if(all)return;pump(app);
    }
    assert(false);
}
static void blocked(X86::Application &app,U handle,U p,U mutex)
{
    const auto &t=app.threads[index(app,handle)];
    assert(app.Read32(p)==1 && !app.Read32(p+4) && t.block==5 && t.wait_handle==mutex && !t.done);
    assert(!app.Runnable(index(app,handle)));
}
static void finished(X86::Application &app,U p,const char *expected)
{
    for(U n=0;n<100 && app.Read32(p)!=2;++n)pump(app);
    assert(app.Read32(p)==2 && app.Read32(p+4)==strlen(expected));
    char output[260];assert(app.Read(app.Read32(p+16),output,sizeof output));assert(!strcmp(output,expected));
    assert(app.Read32(p+20)==app.Read32(p+16)+(strrchr(expected,'\\')-expected)+1);
}
static void unlocked(const X86::Application::Critical &c)
{assert(c.data.available==1 && !c.data.owner && !c.data.recursion && !c.waiting);}
int main(int argc,char **argv)
{
    assert(argc==3);const auto exe=load(argv[1]),font=load(argv[2]);
    for(U profile=0;profile<2;++profile)
    {
        auto *app=new X86::Application(nullptr);
        assert(FixtureModules::KeepNative(*app));
        assert(app->Start(exe.data(),exe.size(),font.data(),font.size(),"COMMON.EXE"));
        app->threads[0].suspended=1;main_thread(*app);
        assert(!app->kernel_paths);
        const U full=code(*app,0x51000000,false),set=code(*app,0x51001000,true);
        const U mutex=profile ? TaskMutex : PathMutex,lock=profile ? 1 : 0;
        // Thread identifiers run through one counter for every process of the
        // run, so the main thread of the second profile is not number one.
        const U main_id=app->threads[0].id;assert(main_id);
        app->KernelPathLock(true,lock);app->KernelPathLock(true,lock);
        auto *held=app->FindCritical(mutex);assert(held && held->data.owner==main_id && held->data.recursion==2);
        if(!profile)unlocked(app->critical_path_task);else unlocked(app->critical_path);
        U reports[3],handles[3];
        if(!profile)
        {
            reports[0]=report(*app,"C:\\FIRST.TXT");reports[1]=report(*app,"C:\\SECOND.TXT");reports[2]=report(*app,"C:\\THIRD.TXT");
            for(U i=0;i<3;++i)handles[i]=spawn(*app,full,reports[i]);
            entered(*app,reports,3);
            assert(app->kernel_paths);auto *published=app->kernel_paths;
            U allocations=0;for(const auto &b:published->buffers)allocations+=b.bytes!=nullptr;
            assert(allocations==ProviderBuffers && !published->locks[0] && !published->locks[1]);
            for(U i=0;i<3;++i)blocked(*app,handles[i],reports[i],PathMutex);
            assert(held->waiting==3 && held->data.recursion==2);unlocked(app->critical_path_task);
            main_thread(*app);app->KernelPathLock(false,0);
            for(U n=0;n<5;++n)pump(*app);
            assert(held->data.recursion==1 && held->data.owner==main_id && app->kernel_paths==published);
            for(U i=0;i<3;++i)blocked(*app,handles[i],reports[i],PathMutex);
            main_thread(*app);app->KernelPathLock(false,0);
            finished(*app,reports[0],"C:\\FIRST.TXT");finished(*app,reports[1],"C:\\SECOND.TXT");finished(*app,reports[2],"C:\\THIRD.TXT");
            assert(app->kernel_paths==published);
        }
        else
        {
            reports[0]=report(*app,"C:\\WINDOWS\\TEMP");handles[0]=spawn(*app,set,reports[0]);
            entered(*app,reports,1);blocked(*app,handles[0],reports[0],TaskMutex);
            assert(app->critical_path.data.owner==app->threads[index(*app,handles[0])].id &&
                   app->critical_path.data.recursion==2 && app->kernel_paths->locks[0]==2);
            reports[1]=report(*app,"SECOND.TXT");reports[2]=report(*app,"THIRD.TXT");
            handles[1]=spawn(*app,full,reports[1]);handles[2]=spawn(*app,full,reports[2]);
            entered(*app,reports,3);
            blocked(*app,handles[1],reports[1],PathMutex);blocked(*app,handles[2],reports[2],PathMutex);
            assert(held->waiting==1 && app->critical_path.waiting==2);
            main_thread(*app);app->KernelPathLock(false,1);
            for(U n=0;n<5;++n)pump(*app);
            blocked(*app,handles[0],reports[0],TaskMutex);
            blocked(*app,handles[1],reports[1],PathMutex);blocked(*app,handles[2],reports[2],PathMutex);
            main_thread(*app);app->KernelPathLock(false,1);
            for(U n=0;n<100 && app->Read32(reports[0])!=2;++n)pump(*app);
            assert(app->Read32(reports[0])==2 && app->Read32(reports[0]+4)==1 && !strcmp(app->files.current,"C:\\WINDOWS\\TEMP"));
            finished(*app,reports[1],"C:\\WINDOWS\\TEMP\\SECOND.TXT");finished(*app,reports[2],"C:\\WINDOWS\\TEMP\\THIRD.TXT");
        }
        unlocked(app->critical_path);unlocked(app->critical_path_task);
        assert(!app->kernel_paths->locks[0] && !app->kernel_paths->locks[1]);
        for(U i=0;i<3;++i){main_thread(*app);const U a[]={handles[i]};assert(app->ThreadApi("CloseHandle",a));}
        delete app;
    }
    puts("KERNEL PATH THREADS PASS: six real x86 workers, recursive Path/Task mutex blocking, final-release wake, isolated scratch results, one lazy provider publication");
}
