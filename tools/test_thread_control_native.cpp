// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <chrono>
#include <thread>
#include "../src/x86_app.h"
#include "system_tree_host.h"
static std::vector<unsigned char> load(const char *path)
{
    FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
    std::vector<unsigned char> b(n);assert(fread(b.data(),1,n,f)==(size_t)n);fclose(f);return b;
}
// Only the loader diagnostics; the call trace of the fixture is not wanted here.
static void loader_note(const char *text)
{
    if(!strncmp(text,"MISSING",7))fprintf(stderr,"%s\n",text);
}
int main(int argc,char **argv)
{
    // MSVCRT.DLL runs as original code since PLAN.md step 7, and the fixture
    // imports _onexit and _cexit, so the guest needs the Win98 tree.
    assert(SystemTreeHost::Mount());
    assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);auto *app=new X86::Application(loader_note);
    if(!app->Start(exe.data(),exe.size(),font.data(),font.size(),"THREADCTL.EXE"))
    {fprintf(stderr,"Thread control guest did not start: %s\n",app->Error());assert(false);}
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while(std::chrono::steady_clock::now()<deadline && app->Status()!=X86::Application::Exited && app->Status()!=X86::Application::Failed)
    {
        app->Pump();
        if(app->Status()==X86::Application::Idle)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if(app->Status()!=X86::Application::Exited || app->ExitCode())fprintf(stderr,"Thread control guest state%d code%u error%s\n",app->Status(),app->ExitCode(),app->Error());
    assert(app->Status()==X86::Application::Exited && !app->ExitCode());
    printf("THREAD CONTROL NATIVE PASS: calls=%u threads=%u\n",app->ApiCalls(),app->ThreadsCreated());delete app;
}
