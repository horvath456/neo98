// SPDX-License-Identifier: GPL-2.0-or-later
#define private public
#include "../src/x86_app.h"
#include "../src/dll_routes.h"
#undef private
#include "../src/files.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>
static std::vector<unsigned char> load(const char*p){FILE*f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
// PLAN.md step 7 put MSVCRT on the original route (src/dll_routes.h), so this
// runner loads the seeded original and asks for no override. The check keeps
// the test honest if the route ever moves back.
int main(int argc,char**argv){assert(argc==9);auto exe=load(argv[1]),font=load(argv[2]),old=load(argv[3]),patch=load(argv[4]),expected=load(argv[5]);auto &store=Files::System();auto codec=load(argv[6]),crt=load(argv[7]),ole=load(argv[8]);assert(!store.Seed("C:\\WINDOWS\\bzip2.exe",codec.data(),codec.size()));assert(!store.Seed("C:\\WINDOWS\\SYSTEM\\MSVCRT.DLL",crt.data(),crt.size()));assert(!store.Seed("C:\\WINDOWS\\SYSTEM\\OLE32.DLL",ole.data(),ole.size()));assert(!store.Seed("C:\\old.bin",old.data(),old.size()));assert(!store.Seed("C:\\patch.bin",patch.data(),patch.size()));X86::Application app([](const char*s){puts(s);});app.trace_calls=true;assert(DllRoutes::Of("MSVCRT.DLL")==DllRoutes::Route::Original);assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"BSPATCH.EXE","bspatch.exe C:\\old.bin C:\\new.bin C:\\patch.bin"));auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);while(app.Status()!=X86::Application::Exited&&app.Status()!=X86::Application::Failed&&std::chrono::steady_clock::now()<deadline){app.Pump(32);X86::Application::PumpChildren(32);}printf("BSPATCH state=%u exit=%u calls=%u error=%s\n",app.Status(),app.ExitCode(),app.ApiCalls(),app.Error());fflush(stdout);assert(app.Status()==X86::Application::Exited&&!app.ExitCode());unsigned node;assert(!store.Lookup("C:\\new.bin",node));const auto *file=store.Get(node);assert(file&&file->size==expected.size()&&!memcmp(file->data,expected.data(),expected.size()));puts("BSPATCH binary output verified");}
