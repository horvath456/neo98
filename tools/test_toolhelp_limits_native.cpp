// SPDX-License-Identifier: GPL-2.0-or-later
#define private public
#include "../src/x86_app.h"
#undef private
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <new>
#include <memory>
static int fail_after=-1;
void *operator new(std::size_t n,const std::nothrow_t &) noexcept {
    if(fail_after==0)return nullptr;if(fail_after>0)--fail_after;
    try{return ::operator new(n);}catch(...){return nullptr;}
}
void *operator new[](std::size_t n,const std::nothrow_t &) noexcept {
    if(fail_after==0)return nullptr;if(fail_after>0)--fail_after;
    try{return ::operator new[](n);}catch(...){return nullptr;}
}
static std::vector<unsigned char>load(const char *path){FILE*f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);auto n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char **argv){
    assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);
    {auto instance=std::make_unique<X86::Application>(nullptr);auto &app=*instance;assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"THLIMIT.EXE"));
     for(unsigned i=0;i<10000&&app.Status()!=X86::Application::Failed&&app.Status()!=X86::Application::Exited;++i)app.Pump(64);
     printf("Toolhelp guest status=%u exit=%u calls=%u error=%s\n",app.Status(),app.ExitCode(),app.ApiCalls(),app.Error());fflush(stdout);
     assert(app.Status()==X86::Application::Exited&&!app.ExitCode());}
    {auto instance=std::make_unique<X86::Application>(nullptr);auto &app=*instance;assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"THOOM.EXE"));app.threads[0].suspended=1;
     // Reject flags before allocation, even if no memory can be obtained.
     for(unsigned flags:{0u,0x80000000u}){
         unsigned invalid[]={flags,0};fail_after=0;
         assert(app.ToolhelpApi("CreateToolhelp32Snapshot",invalid)==~0u);
         assert(app.last_error==87&&fail_after==0&&!app.toolhelp);fail_after=-1;
     }
     const unsigned args[]={2,0};
     fail_after=0;assert(app.ToolhelpApi("CreateToolhelp32Snapshot",args)==~0u);fail_after=-1;assert(app.last_error==8);
     unsigned retained[64];for(auto &h:retained){h=app.ToolhelpApi("CreateToolhelp32Snapshot",args);assert(h!=~0u);}
     // Snapshot header and each of its four row arrays can fail separately.
     for(int failure=0;failure<5;++failure){fail_after=failure;assert(app.ToolhelpApi("CreateToolhelp32Snapshot",args)==~0u);fail_after=-1;assert(app.last_error==8);}
     const auto output=app.Allocate(548);assert(output);
     for(auto h:retained){unsigned size=296;assert(app.Store(output,&size,4));unsigned query[]={h,output};assert(app.ToolhelpApi("Process32First",query));assert(app.ThreadApi("CloseHandle",&h));}
     app.Free(output);}
    puts("Toolhelp limits PASS: invalid flags before allocation/exhaustion, valid inherit flags, more than 64 live public snapshots, handle exhaustion/reuse, duplicates, all row families, six allocation failures; ASan/UBSan");
}
