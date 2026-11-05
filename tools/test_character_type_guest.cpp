// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include <cassert>
#include <cstdio>
#include <vector>
static std::vector<unsigned char> load(const char *name) {
    FILE *f=fopen(name,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
    std::vector<unsigned char> data(n);assert(fread(data.data(),1,n,f)==size_t(n));fclose(f);return data;
}
int main(int argc,char **argv) {
    assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);
    {X86::Application app(nullptr);assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"CTYPE.EXE"));
    for(unsigned n=0;n<10000&&app.Status()!=X86::Application::Exited&&app.Status()!=X86::Application::Failed;++n)app.Pump(32);
    if(app.Status()!=X86::Application::Exited||app.ExitCode())fprintf(stderr,"CTYPE state=%u code=%u error=%s\n",app.Status(),app.ExitCode(),app.Error());
    assert(app.Status()==X86::Application::Exited&&!app.ExitCode());printf("Character type guest PASS calls=%u callbacks=%u\n",app.ApiCalls(),app.Callbacks());}
    assert(!User::LiveWindows()&&!Gdi::LiveObjects());
}
