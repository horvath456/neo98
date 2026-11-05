// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include <cstdio>
#include <vector>
#include <cassert>
static std::vector<unsigned char> load(const char*path){
    FILE*f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);assert(n>0);rewind(f);
    std::vector<unsigned char>data(n);assert(fread(data.data(),1,n,f)==size_t(n));fclose(f);return data;
}
int main(int argc,char**argv){
    assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);X86::Application app(nullptr);
    bool okay=app.Start(exe.data(),exe.size(),font.data(),font.size(),"CRTSET.EXE");
    for(unsigned i=0;okay&&i<400&&app.Status()!=X86::Application::Exited&&app.Status()!=X86::Application::Failed;++i)app.Pump(32);
    okay=okay&&app.Status()==X86::Application::Exited&&!app.ExitCode();
    printf("CRT set search %s state=%u code=%u calls=%u callbacks=%u error=%s\n",okay?"PASS":"FAIL",app.Status(),app.ExitCode(),app.ApiCalls(),app.Callbacks(),app.Error());return okay?0:1;
}
