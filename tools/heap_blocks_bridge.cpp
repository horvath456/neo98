// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_heap_blocks.h"
#include "../src/win98_heap_create.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
using U = unsigned;
[[maybe_unused]] constexpr U ConfigWords = 10;
constexpr U OperationWords = 5, OutputWords = 8192, MaxPages = 2048;
struct Environment
{
    const U *c;
    U *out, commits = 0, fail_commit = 0, fail_reserve = 0, fail_init = 0;
    U events = 0, compat = 0, critical = 0, locks = 0, next_region = 0;
    std::array<U, 32> slots{};
    std::map<U, std::array<unsigned char, 4096>> pages;
    std::map<U, U> regions;
    Win98HeapCreate::State state;
    Environment(const U *config, U *output) : c(config), out(output)
    { out[1] = c[4]; state.critical_sections_ready = c[3]; compat = c[8]; next_region = c[2] + 0x200000; regions[c[2]] = c[1]; }
    void Log(U k, U a=0, U b=0, U d=0, U f=0, U g=0)
    {
        assert(events < 960); U *p = out + 256 + 6 * events++;
        p[0]=k; p[1]=a; p[2]=b; p[3]=d; p[4]=f; p[5]=g;
    }
    U Read8(U a) { auto i = pages.find(a >> 12); assert(i != pages.end()); return i->second[a & 4095]; }
    U Read16(U a) { return Read8(a) | Read8(a+1)<<8; }
    U Read32(U a) { return Read16(a) | Read16(a+2)<<16; }
    void Write8(U a,U v) { auto i=pages.find(a>>12); assert(i!=pages.end()); i->second[a&4095]=v; }
    void Write16(U a,U v) { Write8(a,v); Write8(a+1,v>>8); }
    void Write32(U a,U v) { Write16(a,v); Write16(a+2,v>>16); }
    void Zero(U a,U bytes) { for(U i=0;i<bytes;++i) Write8(a+i,0); }
    void Copy(U dst,U src,U bytes) { for(U i=0;i<bytes;++i) Write8(dst+i,Read8(src+i)); }
    void Error(U e) { Log(9,e); out[1]=e; }
    U CommitPages(U page,U count,U pager,U key,U flags)
    {
        Log(2,page,count,pager,key,flags);
        if (++commits == fail_commit) return 0;
        for(U p=page;p<page+count;++p) if(!pages.count(p)) pages[p].fill(pager==1?0:0xcc);
        return 1;
    }
    U Commit(U p,U n,U f) { return Win98HeapBlocks::Commit(*this,p,n,f); }
    void Decommit(U page,U count,U flags) { Log(4,page,count,flags); for(U p=page;p<page+count;++p) pages.erase(p); }
    U Reserve(U arena,U count,U flags)
    {
        Log(1,arena,count,flags); if(fail_reserve) return ~0u;
        U base=next_region; next_region+=(count<<12)+0x10000; regions[base]=count<<12; return base;
    }
    void Release(U base,U flags)
    { Log(5,base,flags); U n=regions[base]>>12; for(U p=base>>12;p<(base>>12)+n;++p) pages.erase(p); regions.erase(base); }
    void InitializeCriticalSection(U a) { Log(6,a); Zero(a,24); Write32(a+4,0x12345678); }
    void InitializeEarlyLock(U a) { Log(7,a); Write32(a,0x11111111); }
    U InitializeHeap(U root,U base,U size,U flags)
    { Log(3,root,base,size,flags); return fail_init?0:Win98HeapCreate::Initialize(*this,state,root,base,size,flags); }
    void InsertFree(U h,U b,U n,U f) { Win98HeapCreate::InsertFree(*this,h,b,n,f); }
    void EnterCritical() { Log(10); ++critical; }
    void LeaveCritical() { Log(11); assert(critical); --critical; }
    void Lock(U a) { Log(12,a); ++locks; }
    void Unlock(U a) { Log(13,a); assert(locks); --locks; }
    U CompatFlags() { Log(14); return compat; }
    U ProcessHeap() { return c[2]; }
    void RaiseNoMemory(U bytes) { Log(15,bytes); }
    void Run(const U *ops)
    {
        const U heap=c[2];
        out[0]=Win98HeapCreate::Initialize(*this,state,heap,heap,c[1],c[0]);
        fail_commit=c[5]?commits+c[5]:0; fail_reserve=c[6]; fail_init=c[7];
        assert(c[9]<=128);
        for(U i=0;i<c[9];++i)
        {
            const U *a=ops+OperationWords*i; U &p=slots[a[1]%slots.size()], r=0;
            switch(a[0])
            {
            case 0: r=Win98HeapBlocks::HeapAlloc(*this,heap,a[2],a[3]); p=r; break;
            case 1: r=Win98HeapBlocks::HeapFree(*this,heap,a[2],p); break;
            case 2: r=Win98HeapBlocks::HeapSize(*this,heap,a[2],p); break;
            case 3: r=Win98HeapBlocks::HeapReAlloc(*this,heap,a[2],p,a[3]); if(r)p=r; break;
            case 4: for(U j=0;j<a[3];++j) Write8(p+j,a[4]+j); r=p; break;
            case 5: Write32(heap+a[3],a[4]); break;
            case 6: fail_commit=a[2]?commits+a[2]:0; fail_reserve=a[3]; fail_init=a[4]; break;
            case 7: r=Win98HeapBlocks::Reallocate(*this,heap,p,a[3],a[2]); if(r)p=r; break;
            case 8: r=Win98HeapBlocks::Allocate(*this,heap,a[3],a[2]); p=r; break;
            case 9: r=Win98HeapBlocks::HeapSize(*this,heap,a[2],heap+a[3]); break;
            case 10: r=Win98HeapBlocks::HeapFree(*this,heap,a[2],heap+a[3]); break;
            case 11: compat=a[3]; break;
            default: assert(false);
            }
            out[32+i]=r;
        }
        out[2]=events; out[3]=pages.size(); out[4]=critical; out[5]=locks;
        for(U i=0;i<slots.size();++i)out[192+i]=slots[i];
    }
};
extern "C" void heap_blocks(const U *config,const U *ops,U *output,unsigned char *snapshot)
{
    std::memset(output,0,OutputWords*sizeof(U));
    Environment e(config,output); e.Run(ops); assert(e.pages.size()<=MaxPages);
    U n=0; for(const auto &p:e.pages) { output[6144+n]=p.first; std::memcpy(snapshot+n++*4096,p.second.data(),4096); }
}
#ifdef HEAP_BLOCKS_MAIN
int main(int argc,char **argv)
{
    assert(argc==2); FILE *f=std::fopen(argv[1],"rb"); assert(f);
    U config[ConfigWords], output[OutputWords], expected[OutputWords], count=0;
    std::vector<unsigned char> snapshot(MaxPages*4096), wanted(MaxPages*4096);
    while(std::fread(config,sizeof config,1,f))
    {
        std::vector<U> ops(config[9]*OperationWords);
        assert(std::fread(ops.data(),sizeof(U)*ops.size(),1,f)==1);
        assert(std::fread(expected,sizeof expected,1,f)==1);
        const U bytes=expected[3]*4096; assert(std::fread(wanted.data(),bytes,1,f)==1);
        heap_blocks(config,ops.data(),output,snapshot.data());
        assert(!std::memcmp(output,expected,sizeof output)); assert(!std::memcmp(snapshot.data(),wanted.data(),bytes)); ++count;
    }
    assert(std::feof(f));std::fclose(f);std::printf("HEAP BLOCKS NATIVE PASS %u sequences\n",count);
}
#endif
