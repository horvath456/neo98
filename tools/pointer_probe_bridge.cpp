// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_pointer_probe.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using U=unsigned;
namespace {
constexpr U Data=0x30000000, Sum=0xbffca054, Add=0xbffca058, Bytes=9*4096;
unsigned char memory[Bytes];
std::vector<U> trace;
struct Environment {
    const U *a; Win98PointerProbe::State &state; U faults=0;
    unsigned char *Byte(U p,bool write) {
        if(p==Sum)return &state.read_sum;
        if(p==Add)return &state.write_add;
        U page,offset;
        if(p>=Data && p-Data<8*4096) { offset=p-Data;page=offset>>12;
            if(!(a[8]&(1u<<page)) || !(a[6]&(1u<<page)) || (write && !(a[7]&(1u<<page))))return nullptr;
        } else if(p>=0xfffff000) {offset=8*4096+(p&4095);
            if(!(a[9]&4) || !(a[9]&1) || (write && !(a[9]&2)))return nullptr;
        } else return nullptr;
        return memory+offset;
    }
    bool ReadByte(U p,U &value) {trace.push_back(1);trace.push_back(p);auto*b=Byte(p,false);if(!b){++faults;return false;}value=*b;return true;}
    bool AddByte(U p,U increment) {trace.push_back(2);trace.push_back(p);auto*b=Byte(p,true);if(!b){++faults;return false;}*b=static_cast<unsigned char>(*b+increment);return true;}
};
}
extern "C" void pointer_probe_run(const U*a,U*out) {
    trace.clear();for(U i=0;i<Bytes;++i)memory[i]=65+(i+a[11])%26;
    if(a[10]<Bytes)memory[a[10]]=0;
    Win98PointerProbe::State state{static_cast<unsigned char>(a[3]),static_cast<unsigned char>(a[4])};Environment e{a,state};
    U result=0;
    switch(a[0]) {
    case 0:result=Win98PointerProbe::Code(e,state,a[1]);break;
    case 1:case 3:result=Win98PointerProbe::Read(e,state,a[1],a[2]);break;
    case 2:case 6:result=Win98PointerProbe::Write(e,state,a[1],a[2]);break;
    case 4:result=Win98PointerProbe::String(e,a[1],a[2]);break;
    case 5:result=Win98PointerProbe::WideString(a[1],a[2]);break;
    default:std::abort();
    }
    out[0]=result;out[1]=state.read_sum;out[2]=state.write_add;out[3]=a[5];out[4]=e.faults;
}
extern "C" const unsigned char *pointer_probe_memory(){return memory;}
extern "C" const U *pointer_probe_trace(U*size){*size=static_cast<U>(trace.size());return trace.data();}
#ifdef POINTER_PROBE_STANDALONE
int main(int argc,char**argv) {
    if(argc!=2)return 2;FILE*f=std::fopen(argv[1],"rb");if(!f)return 3;U cases;
    if(std::fread(&cases,4,1,f)!=1)return 4;
    for(U i=0;i<cases;++i){U a[16],expected[5],out[5],count;unsigned char bytes[Bytes];
        if(std::fread(a,4,16,f)!=16 || std::fread(expected,4,5,f)!=5 || std::fread(bytes,1,Bytes,f)!=Bytes || std::fread(&count,4,1,f)!=1)return 5;
        std::vector<U> ops(count);if(std::fread(ops.data(),4,count,f)!=count)return 6;
        pointer_probe_run(a,out);if(std::memcmp(out,expected,sizeof out) || std::memcmp(bytes,memory,Bytes) || ops!=trace){std::fprintf(stderr,"Pointer case %u mismatch\n",i);return 7;}
    }
    std::fclose(f);std::printf("Pointer-probe original corpus: %u cases PASS (ASan/UBSan)\n",cases);return 0;
}
#endif
