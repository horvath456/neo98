// SPDX-License-Identifier: GPL-2.0-or-later
#define private public
#include "../src/x86_app.h"
#undef private
#include "../src/win98_memory_status.h"
#include <cassert>
#include <cstdio>
#include <vector>
static std::vector<unsigned char> load(const char*p){FILE*f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char**argv){assert(argc==3);unsigned baseline=0;assert(VirtualMemory::Space::GlobalMappedPages(baseline));{
 VirtualMemory::Space extra;assert(extra.MappedPages()==0&&extra.FreePrivatePages()==0x7fc00);assert(extra.Track(0x400000,12288,4));unsigned pages=0;assert(extra.MappedPages()==3&&VirtualMemory::Space::GlobalMappedPages(pages)&&pages==baseline+3);extra.Forget(0x400000,12288);assert(extra.FreePrivatePages()==0x7fc00&&extra.MappedPages()==0);
 }
 {auto exe=load(argv[1]),font=load(argv[2]);X86::Application app(nullptr);assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"MEMSTAT.EXE"));for(unsigned i=0;i<1000&&app.Status()!=X86::Application::Exited&&app.Status()!=X86::Application::Failed;++i)app.Pump(32);if(app.Status()!=X86::Application::Exited||app.ExitCode())fprintf(stderr,"Memory status state=%u exit=%u calls=%u error=%s\n",app.Status(),app.ExitCode(),app.ApiCalls(),app.Error());assert(app.Status()==X86::Application::Exited&&!app.ExitCode());// GlobalMemoryStatus and DPMI 0500h come from one demand report.
 unsigned demand[14]{},out[8]{},dpmi[12]{};assert(app.GuestDemandInfo(demand)&&app.DpmiFreeMemoryInfo(dpmi)&&Win98MemoryStatus::Status(demand,out));
 assert(dpmi[8]&&dpmi[8]!=0xffffffffu&&out[4]==dpmi[8]<<12&&out[2]==dpmi[6]<<12&&out[5]<=out[4]&&dpmi[3]==dpmi[6]+dpmi[8]&&dpmi[0]==dpmi[1]<<12&&dpmi[1]<=2*dpmi[6]&&dpmi[7]<=dpmi[1]);
 printf("Memory status PASS calls=%u callbacks=%u mapped_pages=%u free_private_pages=%u\n",app.ApiCalls(),app.Callbacks(),app.virtual_memory.MappedPages(),app.virtual_memory.FreePrivatePages());}
 unsigned after=0;assert(VirtualMemory::Space::GlobalMappedPages(after)&&after==baseline);
}
