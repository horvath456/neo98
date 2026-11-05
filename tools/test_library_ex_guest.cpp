// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <cstring>
#include <vector>
static std::vector<unsigned char> load(const char*p){FILE*f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char**argv){assert(argc==4);auto exe=load(argv[1]),font=load(argv[2]);for(const char*dir:{"C:\\ALT"})assert(!Files::System().MakeDirectory(dir));const char*names[]={"ENTRY.DLL","DEP3.DLL","DEP7.DLL"};const char*targets[]={"C:\\ALT\\ENTRY.DLL","C:\\DEP.DLL","C:\\ALT\\DEP.DLL"};for(unsigned i=0;i<3;++i){auto b=load((std::string(argv[3])+"/"+names[i]).c_str());assert(!Files::System().Seed(targets[i],b.data(),b.size()));}unsigned key=0;bool created=false;const Registry::Value*value=nullptr;auto &registry=Registry::System();assert(!registry.Open(registry.roots[0],"CLSID\\{ECD4FC4D-521C-11D0-B792-00A0C90312E1}\\InProcServer32",false,key,created));assert(!registry.Query(key,"",value)&&value->type==2&&!strcmp((const char*)value->data,"C:\\WINDOWS\\SYSTEM\\BROWSEUI.DLL"));X86::Application app(nullptr);assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"C:\\LIBRARY.EXE"));for(unsigned i=0;i<1000&&app.Status()!=X86::Application::Exited&&app.Status()!=X86::Application::Failed;++i)app.Pump(32);if(app.Status()!=X86::Application::Exited||app.ExitCode())fprintf(stderr,"LibraryEx state=%u exit=%u calls=%u error=%s\n",app.Status(),app.ExitCode(),app.ApiCalls(),app.Error());assert(app.Status()==X86::Application::Exited&&!app.ExitCode());printf("Original LoadLibraryExA PASS calls=%u callbacks=%u\n",app.ApiCalls(),app.Callbacks());}
