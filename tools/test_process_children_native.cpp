/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/x86_app.h"
#include <cassert>
#include <cstdio>
#include <vector>
static void log(const char*p){puts(p);}
static std::vector<unsigned char>load(const char*path){FILE*f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
static void pump(X86::Application&a){for(unsigned i=0;i<500&&a.Status()!=X86::Application::Failed&&a.Status()!=X86::Application::Exited;++i)a.Pump(4);if(a.Status()!=X86::Application::Exited||a.ExitCode()){fprintf(stderr,"FAIL pid=%u status=%u code=%u %s children=%u\n",a.ProcessId(),a.Status(),a.ExitCode(),a.Error(),X86::Application::LiveChildren());assert(false);}}
int main(int argc,char**argv){assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);auto&files=Files::System();assert(!files.MakeDirectory("C:\\CHILD DIR"));assert(!files.Seed("C:\\CHILD DIR\\CHILD.EXE",exe.data(),exe.size()));
 auto*a=new X86::Application(log);assert(a->Start(exe.data(),exe.size(),font.data(),font.size(),"PARENT.EXE","PARENT"));pump(*a);unsigned calls=a->ApiCalls();delete a;assert(!X86::Application::LiveChildren()&&!X86::Application::ChildRecords());
 assert(!files.Seed("C:\\CHILD DIR\\CHILD.EXE",exe.data(),exe.size()));a=new X86::Application(log);assert(a->Start(exe.data(),exe.size(),font.data(),font.size(),"ORPHAN.EXE","ORPHAN"));
 // A single low-budget parent pump publishes the child and exits before it
 // receives any CPU time. Subsequent work is entirely manager-owned.
 while(a->Status()!=X86::Application::Exited&&a->Status()!=X86::Application::Failed){a->Pump(1);if(X86::Application::LiveChildren())break;}
 // Parent can still be on an API slice boundary; resume until its exit. A
 // command fixture Sleep is unnecessary: child waits for a host file below.
 pump(*a);unsigned orphan_calls=a->ApiCalls();assert(X86::Application::LiveChildren()==1);delete a;assert(X86::Application::LiveChildren()==1);const unsigned go=1;assert(!files.Seed("C:\\WINDOWS\\TEMP\\ORPHAN-GO.DAT",&go,4));
 for(unsigned i=0;i<100&&X86::Application::LiveChildren();++i)X86::Application::PumpChildren(4);
 assert(!X86::Application::LiveChildren()&&!X86::Application::ChildRecords());unsigned id;assert(!files.Lookup("C:\\WINDOWS\\TEMP\\ORPHAN.DAT",id));const auto*n=files.Get(id);assert(n->size==4&&n->data[0]==77);assert(!User::LiveWindows());
 // a helper process that only knows the identity of the program that
 // started it opens that process and waits on the handle. The parent runs to
 // its own exit while the helper waits, so the wait may only end there.
 assert(!files.Seed("C:\\CHILD DIR\\CHILD.EXE",exe.data(),exe.size()));
 a=new X86::Application(log);assert(a->Start(exe.data(),exe.size(),font.data(),font.size(),"FOREIGN.EXE","FOREIGN"));
 for(unsigned i=0;i<2000&&a->Status()!=X86::Application::Exited&&a->Status()!=X86::Application::Failed;++i)a->Pump(4);
 if(a->Status()!=X86::Application::Exited||a->ExitCode()!=55){fprintf(stderr,"FAIL foreign parent status=%u code=%u %s\n",a->Status(),a->ExitCode(),a->Error());assert(false);}
 assert(X86::Application::LiveChildren()==1);
 for(unsigned i=0;i<2000&&X86::Application::LiveChildren();++i)X86::Application::PumpChildren(4);
 assert(!X86::Application::LiveChildren());
 assert(!files.Lookup("C:\\WINDOWS\\TEMP\\FOREIGN.DAT",id));n=files.Get(id);assert(n->size==4&&n->data[0]==55);
 unsigned foreign_calls=a->ApiCalls();delete a;
 printf("CHILD PROCESSES PASS parent_calls=%u orphan_parent_calls=%u foreign_parent_calls=%u\n",calls,orphan_calls,foreign_calls);
}
