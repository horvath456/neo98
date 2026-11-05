// SPDX-License-Identifier: GPL-2.0-or-later
// independently learn accesses within one translation, preserve rights.
#include "../src/x86_cpu.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using X86::CpuError;
using X86::Prot;
using X86::Reg;
static void okay(unsigned v) { assert(v == CpuError::Ok); }
static void word(std::vector<unsigned char> &code, unsigned v)
{
    for (unsigned i=0; i<4; ++i) code.push_back(v >> (8*i));
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    const unsigned mode = strtoul(argv[1], nullptr, 0);
    constexpr unsigned Code=0x10000, Plain=0x28000, Stack=0x40000, Back=0x50000;
    const unsigned Data=mode==6?0x12345000:0x23000;
    const unsigned address=Data+(mode==7);
    X86::Cpu *cpu=nullptr;
    okay(X86::CreateCpu(X86::ProcessCpuBackend(), cpu));
    okay(cpu->Map(Code,0x8000,Prot::All));
    okay(cpu->Map(Data-0x3000,0x3000,Prot::Read));
    okay(cpu->Map(Data,0x1000,Prot::Read|Prot::Write));
    okay(cpu->Map(Plain,0x4000,Prot::Read|Prot::Write));
    okay(cpu->Map(Stack,0x4000,Prot::Read|Prot::Write));
    okay(cpu->Map(Back,0x4000,Prot::All));
    const unsigned char nop=0x90;
    okay(cpu->Write(Back,&nop,1));
    const unsigned start=Code+0xff8;
    std::vector<unsigned char> code;
    if (mode <= 1 || mode >= 4) {
        // Two stores on opposite sides of a 4-KiB code boundary, in one block.
        if(mode>=5) { code.push_back(0xa3); word(code,address); }
        else { code.push_back(0x89); code.push_back(0x06); } // mov [esi],eax
        for(unsigned i=0;i<14;++i) code.push_back(0x90);
        if(mode>=5) { code.push_back(0xa3); word(code,address+4); }
        else { code.push_back(0x89); code.push_back(0x07); } // mov [edi],eax
    } else {
        // A data-layout timing probe. All ordinary accesses remain identical.
        code.push_back(0x89); code.push_back(0x06); // mov [esi],eax
        for(unsigned i=0;i<32;++i) {
            code.push_back(0x03); code.push_back(0x03); // add eax,[ebx]
        }
        code.push_back(0x49); // dec ecx
        code.push_back(0x0f); code.push_back(0x85); word(code,0-(code.size()+4));
    }
    code.push_back(0xc3);
    okay(cpu->Write(start,code.data(),code.size()));
    okay(cpu->Protect(Code,0x8000,Prot::Read|Prot::Exec));
    const unsigned other=Back+0x100;
    std::vector<unsigned char> jump={0xe9}; word(jump,start-(other+5));
    okay(cpu->Write(other,jump.data(),jump.size()));
    okay(cpu->FlushCode());
    auto run=[&](unsigned esi,unsigned edi,unsigned count, unsigned entry=0) {
        const unsigned esp=Stack+0x3ff0;
        okay(cpu->Write(esp,&Back,4));
        okay(cpu->Set(Reg::Esp,esp)); okay(cpu->Set(Reg::Esi,esi)); okay(cpu->Set(Reg::Edi,edi));
        okay(cpu->Set(Reg::Ebx,Plain)); okay(cpu->Set(Reg::Ecx,count)); okay(cpu->Set(Reg::Eax,0x12345678));
        return cpu->Run(entry?entry:start,Back,0);
    };
    if(mode<=1 || mode>=4) {
        if(mode==4) { okay(run(Plain,Plain+4,0)); okay(run(Plain,Plain+4,0,other)); }
        for(unsigned i=0;i<32;++i) okay(run(Data,Data+4,0));
        if(mode==4) for(unsigned i=0;i<32;++i) okay(run(Data,Data+4,0,other));
        unsigned values[2]={}; okay(cpu->Read(address,values,sizeof values));
        assert(values[0]==0x12345678 && values[1]==0x12345678);
        if(mode==1 || mode>=5) {
            // The already checked instructions must still fault on.rdata,
            // unmapped memory, and on changed rights without changing code.
            const unsigned untouched=0xaabbccdd;
            okay(cpu->Write(Data-4,&untouched,4));
            if(mode==1) assert(run(Data-4,Data+4,0)==CpuError::WriteProtected);
            unsigned value=0; okay(cpu->Read(Data-4,&value,4)); assert(value==untouched);
            okay(cpu->Protect(Data,0x1000,Prot::Read));
            assert(run(Data,Data+4,0)==CpuError::WriteProtected);
            okay(cpu->Protect(Data,0x1000,Prot::Read|Prot::Write));
            okay(run(Data,Data+4,0));
            okay(cpu->Unmap(Data,0x1000));
            assert(run(Data,Data+4,0)==CpuError::WriteUnmapped);
            okay(cpu->Map(Data,0x1000,Prot::Read|Prot::Write));
            okay(run(Data,Data+4,0));
        }
        printf("CPU DATA PLACEMENT PASS mode=%u\n",mode);
    } else {
        const unsigned data=mode==2?Data:Plain+4, rounds=3000000;
        okay(run(data,data,1000));
        const auto begin=std::chrono::steady_clock::now();
        okay(run(data,data,rounds));
        const auto ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-begin).count();
        printf("DATA BENCH mode=%u rounds=%u ns_per_loop=%.3f\n",mode,rounds,ns/rounds);
    }
    delete cpu;
}
