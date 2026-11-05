// SPDX-License-Identifier: GPL-2.0-or-later
#define private public
#include "../src/x86_app.h"
#undef private
#include "../src/files.h"
#include "../src/dll_routes.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <vector>
static std::vector<unsigned char> load(const char*p){FILE*f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
// PLAN.md step 7 put MSVCRT on the original route (src/dll_routes.h), so this
// runner loads the seeded original and asks for no override. The check keeps
// the test honest if the route ever moves back.
int main(int argc,char**argv){
 assert(argc==10);auto exe=load(argv[1]),font=load(argv[2]),crt=load(argv[3]),ole=load(argv[4]),input=load(argv[5]);auto &store=Files::System();
 assert(!store.Seed("C:\\WINDOWS\\SYSTEM\\MSVCRT.DLL",crt.data(),crt.size()));assert(!store.Seed("C:\\WINDOWS\\SYSTEM\\OLE32.DLL",ole.data(),ole.size()));assert(!store.Seed(argv[6],input.data(),input.size()));
 X86::Application app([](const char*s){puts(s);});app.trace_calls=true;assert(DllRoutes::Of("MSVCRT.DLL")==DllRoutes::Route::Original);assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"CODEC.EXE",argv[7]));
 auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);while(app.Status()!=X86::Application::Exited&&app.Status()!=X86::Application::Failed&&std::chrono::steady_clock::now()<deadline){app.Pump(32);X86::Application::PumpChildren(32);}
 printf("CONSOLE CODEC state=%u exit=%u calls=%u error=%s\n",app.Status(),app.ExitCode(),app.ApiCalls(),app.Error());fflush(stdout);assert(app.Status()==X86::Application::Exited&&!app.ExitCode());
 unsigned node;assert(!store.Lookup(argv[8],node));const auto *file=store.Get(node);assert(file&&file->size);FILE*out=fopen(argv[9],"wb");assert(out);assert(fwrite(file->data,1,file->size,out)==file->size);assert(!fclose(out));
}
