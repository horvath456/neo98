// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include <cassert>
#include <cstdio>
#include <vector>
static void log(const char*text){puts(text);}
static std::vector<unsigned char>load(const char*path){
    FILE*f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
    std::vector<unsigned char>data(n);assert(fread(data.data(),1,n,f)==size_t(n));fclose(f);return data;
}
int main(int argc,char**argv){
    assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);auto*app=new X86::Application(log);
    assert(app->Start(exe.data(),exe.size(),font.data(),font.size(),"CRTFPU.EXE","TEST"));
    for(unsigned i=0;i<400&&app->Status()!=X86::Application::Failed&&app->Status()!=X86::Application::Exited;++i)app->Pump(8);
    bool okay=app->Status()==X86::Application::Exited&&!app->ExitCode();
    printf("CRT CRTFPU %s state=%u code=%u calls=%u error=%s\n",okay?"PASS":"FAIL",app->Status(),app->ExitCode(),app->ApiCalls(),app->Error());
    delete app;return okay?0:1;
}
