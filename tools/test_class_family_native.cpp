// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include <vector>
#include <cstring>
#include <memory>
#define private public
#include "../src/x86_app.h"
#undef private
#include "../src/user.cpp"
static std::vector<unsigned char>load(const char*p){FILE*f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);auto n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char**argv){assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);{auto instance=std::make_unique<X86::Application>(nullptr);auto &app=*instance;assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"CLASSES.EXE"));for(unsigned n=0;n<10000&&app.Status()!=X86::Application::Failed&&app.Status()!=X86::Application::Exited;++n)app.Pump(32);fprintf(stderr,"class state=%u code=%u error=%s calls=%u\n",app.Status(),app.ExitCode(),app.Error(),app.ApiCalls());assert(app.Status()==X86::Application::Exited&&!app.ExitCode());printf("CLASS FAMILY GUEST PASS calls=%u callbacks=%u\n",app.ApiCalls(),app.Callbacks());}{
 auto instance=std::make_unique<X86::Application>(nullptr);auto &app=*instance;assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"CLASSES.EXE"));app.threads[0].suspended=1;
 unsigned f[12]={48,0,0x401000,40,0,0x400000};char name[64];unsigned count=0;
 for(unsigned i=0;i<2000;++i){snprintf(name,sizeof name,"ClassCapacity%u",i);if(!app.RegisterNativeClass(f,name)){assert(User::NativeClassError()==8);break;}++count;}assert(count>400&&count<2000);
 snprintf(name,sizeof name,"ClassCapacity%u",count);assert(!app.RegisterNativeClass(f,name));
 for(unsigned i=0;i<count;++i){snprintf(name,sizeof name,"ClassCapacity%u",i);assert(User::UnregisterNativeClass(0x400000,name));}
 assert(app.RegisterNativeClass(f,"AfterClassOOM"));assert(User::UnregisterNativeClass(0x400000,"AfterClassOOM"));
 // Owner-draw callbacks query class icons. Queries must not schedule another
 // composition, or the original Taskman never reaches its input wait.
 assert(User::RegisterClass("QueryWithoutRepaint",0,0,16));
 unsigned query_window=User::CreateWindow("QueryWithoutRepaint","",0,0,0,20,20,0,0,0);
 assert(query_window);User::dirty=false;
 User::ClassAccess(query_window,-14,0,2);assert(!User::Dirty());
 User::ClassAccess(query_window,-26,0,2);assert(!User::Dirty());
 assert(User::DestroyWindow(query_window));assert(User::UnregisterNativeClass(1,"QueryWithoutRepaint"));
 // Failed lookup must not allocate native module translation slots.
 unsigned info[12]={48};for(unsigned i=0;i<800;++i)assert(!app.GetNativeClassInfo(0x800000+i,"MissingClass",info));assert(app.RegisterNativeClass(f,"AfterModuleLookup"));
 // Menu text is copied by original USER registration. Publication is owned by
 // the application and freed only when the successful unregister is observed.
 char menu[32]="FirstClassMenu";f[9]=0x400123;assert(app.RegisterNativeClass(f,"OwnedMenu",menu));strcpy(menu,"ChangedSource");assert(app.GetNativeClassInfo(0x400000,"OwnedMenu",info));unsigned published=app.PublishClassMenu(info[9]);assert(!strcmp(app.ReadString(published),"FirstClassMenu"));
 unsigned guest_name=app.Allocate(32);app.Store(guest_name,"OwnedMenu",10);unsigned args[2]={guest_name,0x400000};assert(app.ClassApi("UnregisterClassA",args));for(const auto&lease:app.class_menu_leases)assert(!lease.token);app.Free(guest_name);
 puts("CLASS FAMILY CAPACITY PASS: more than 400 application classes, USER heap OOM rollback/reuse, lookup identity, copied menu lifetime");
 }
 assert(!User::class_store.Read16(0x1814)&&!User::class_store.Read16(0xfa));puts("CLASS FAMILY NATIVE PASS");}
