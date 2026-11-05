// SPDX-License-Identifier: GPL-2.0-or-later
// Isolated provider test. Include the production translation unit to inspect
// storage accounting without exporting test APIs or changing the original core.
#include <stdlib.h>
static int fail_allocation_after=-1;
static void *ProviderMalloc(size_t bytes)
{
    if(fail_allocation_after==0) { fail_allocation_after=-1; return nullptr; }
    if(fail_allocation_after>0) --fail_allocation_after;
    return ::malloc(bytes);
}
// Only the production storage provider's explicit malloc calls are injected.
#define malloc ProviderMalloc
#include "../src/gdi.cpp"
#undef malloc
#include <cstdio>
#include <cstring>
#include <vector>

using namespace Gdi;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "Bitmap provider edge line %d: %s\n", __LINE__, #x); return 1; } } while (0)
struct Memory final : BitmapIO
{
    unsigned char data[256] = {};
    unsigned readable = sizeof data, writable = sizeof data, faults = 0, error = 0;
    bool Range(unsigned p, unsigned n, unsigned limit) const
    { return p >= 0x1000 && p - 0x1000 < limit && n <= limit - (p - 0x1000); }
    bool Read(unsigned p, void *out, unsigned n) override
    { if (!Range(p,n,readable)) return false; std::memcpy(out,data+(p-0x1000),n); return true; }
    bool Write(unsigned p, const void *in, unsigned n) override
    { if (!Range(p,n,writable)) return false; std::memcpy(data+(p-0x1000),in,n); return true; }
    bool Probe(unsigned p, bool write) override { return Range(p,1,write ? writable : readable); }
    void Error(unsigned e) override { error=e; }
    void Fault(const char *) override { ++faults; }
    unsigned Call(const char *name,unsigned a=0,unsigned b=0,unsigned c=0,unsigned d=0,unsigned e=0)
    { const unsigned args[]={a,b,c,d,e}; return BitmapApi(name,args,*this); }
};
struct BitmapProviderSnapshot
{
    unsigned objects=LiveObjects(), bytes=bitmap_store.allocated, ranges=0, globals=0, locals=0, locks=0;
    BitmapProviderSnapshot()
    {
        for(unsigned i=0;i<bitmap_store.range_capacity;++i) ranges+=bitmap_store.ranges[i].base!=0;
        for(const auto &g:bitmap_store.globals) { globals+=g.range!=0; locks+=g.locks; }
        for(const auto &l:bitmap_store.locals) locals+=l.native!=0;
    }
    bool operator==(const BitmapProviderSnapshot &b) const
    { return objects==b.objects && bytes==b.bytes && ranges==b.ranges && globals==b.globals && locals==b.locals && locks==b.locks; }
};

int main()
{
    Memory m;
    const BitmapProviderSnapshot empty;
    CHECK(!empty.objects && !empty.bytes && !empty.ranges);
    fail_allocation_after=0;
    CHECK(!m.Call("CreateBitmap",8,8,1,1,0) && m.error==8 && BitmapProviderSnapshot()==empty);
    // The guest source starts valid but fails halfway through the copy. Both
    // bootstrap stock storage and the interrupted new DDB must be reclaimed.
    m.readable=8;
    CHECK(!m.Call("CreateBitmap",8,8,1,1,0x1000) && m.faults==1);
    CHECK(BitmapProviderSnapshot()==empty && !bitmap_store.io && !bitmap_store.state.empty_bitmap);
    m.readable=sizeof m.data;
    const unsigned bitmap=m.Call("CreateBitmap",8,8,1,1,0);
    CHECK(bitmap);
    const BitmapProviderSnapshot one;
    fail_allocation_after=0;
    CHECK(!m.Call("CreateBitmap",8,8,1,1,0) && m.error==87 && BitmapProviderSnapshot()==one);
    // Fail the segmented bits allocation, then the linear mirror allocation.
    fail_allocation_after=1;
    CHECK(!m.Call("CreateBitmap",240,3000,1,1,0) && m.error==87 && BitmapProviderSnapshot()==one);
    fail_allocation_after=2;
    CHECK(!m.Call("CreateBitmap",240,3000,1,1,0) && m.error==8 && BitmapProviderSnapshot()==one);
    fail_allocation_after=0;
    CHECK(!CreatePatternBrush(bitmap) && BitmapProviderSnapshot()==one);
    // A transfer failure must preserve existing handles and their ownership;
    // already transferred pixels are deliberately not made transactional.
    m.readable=8;
    CHECK(!m.Call("SetBitmapBits",bitmap,16,0x1000) && m.faults==2);
    CHECK(ObjectExists(bitmap) && BitmapProviderSnapshot()==one && !bitmap_store.io);
    m.readable=sizeof m.data; m.writable=7;
    CHECK(!m.Call("GetObjectA",bitmap,84,0x1000) && m.faults==3);
    CHECK(BitmapProviderSnapshot()==one && ObjectExists(bitmap));
    CHECK(!m.Call("GetBitmapBits",bitmap,16,0x1000) && m.faults==4);
    CHECK(BitmapProviderSnapshot()==one && ObjectExists(bitmap));
    m.writable=sizeof m.data;
    CHECK(m.Call("GetBitmapBits",bitmap,16,0x1000)==16 && BitmapProviderSnapshot()==one);
    CHECK(!m.Call("SetBitmapBits",bitmap,0xffffffff,0x1000) && m.error==87);
    CHECK(!m.Call("GetBitmapBits",bitmap,32,0xfffffff0) && m.error==87 && BitmapProviderSnapshot()==one);

    // Fill the public object table; failed original LocalAllocate must release
    // the global storage allocated before it, and leave stock/source intact.
    std::vector<unsigned> filler;
    while(const auto brush=CreateSolidBrush(0)) filler.push_back(brush);
    const BitmapProviderSnapshot full;
    CHECK(!m.Call("CreateBitmap",8,8,1,1,0) && m.error==87 && BitmapProviderSnapshot()==full);
    CHECK(!CreatePatternBrush(bitmap) && BitmapProviderSnapshot()==full);
    CHECK(DeleteObject(filler.back())); filler.pop_back();
    const BitmapProviderSnapshot one_slot;
    // A pattern needs a brush record and a temporary DDB record. Failure after
    // the first one must restore both the original source and all accounting.
    CHECK(!CreatePatternBrush(bitmap) && BitmapProviderSnapshot()==one_slot);
    for(auto handle:filler) CHECK(DeleteObject(handle));
    CHECK(BitmapProviderSnapshot()==one);

    // alternating region/input mappings, short accesses, shrinking,
    // unmap/reuse and external IO must all retain the checked-memory contract.
    unsigned char bytes[4][4]={{1,2,3,4},{5,6,7,8},{9,10,11,12},{13,14,15,16}};
    unsigned range[4], address[4];
    for(unsigned i=0;i<4;++i) {
        range[i]=bitmap_store.Map(bytes[i],4);CHECK(range[i]);
        address[i]=bitmap_store.ranges[range[i]-1].base;
    }
    for(unsigned round=0;round<1000;++round)for(unsigned i=0;i<4;++i) {
        const unsigned value=bytes[i][0]|(unsigned(bytes[i][1])<<8)|(unsigned(bytes[i][2])<<16)|(unsigned(bytes[i][3])<<24);
        CHECK(bitmap_store.Read32(address[i])==value);
        bitmap_store.Write16(address[i]+1,round);CHECK(bytes[i][1]==(round&255)&&bytes[i][2]==(round>>8));
    }
    bitmap_store.ranges[range[0]-1].size=2;
    bitmap_store.failed=false;CHECK(!bitmap_store.Read32(address[0])&&bitmap_store.failed);
    bitmap_store.ranges[range[0]-1].size=4;
    bitmap_store.failed=false;CHECK(!bitmap_store.Read16(address[0]+3)&&bitmap_store.failed);
    bitmap_store.Unmap(range[1]);
    bitmap_store.failed=false;CHECK(!bitmap_store.Read8(address[1])&&bitmap_store.failed);
    unsigned reused_range=bitmap_store.Map(bytes[1],4);CHECK(reused_range==range[1]);
    CHECK(bitmap_store.Read8(bitmap_store.ranges[reused_range-1].base)==bytes[1][0]);
    bitmap_store.failed=false;CHECK(!bitmap_store.Read8(address[1])&&bitmap_store.failed);
    for(auto mapped:range)bitmap_store.Unmap(mapped);
    const unsigned external=bitmap_store.Map(nullptr,4,&m,0x1000);CHECK(external);
    const unsigned external_address=bitmap_store.ranges[external-1].base;
    m.data[0]=42;CHECK(bitmap_store.Read8(external_address)==42);
    m.data[0]=43;CHECK(bitmap_store.Read8(external_address)==43);
    m.readable=0;bitmap_store.failed=false;CHECK(!bitmap_store.Read8(external_address)&&bitmap_store.failed);
    m.readable=sizeof m.data;bitmap_store.failed=false;bitmap_store.Unmap(external);
    CHECK(BitmapProviderSnapshot()==one);

    // Descriptors grow beyond the former 400-slot limit. A mapping larger
    // than the provider address space must fail without changing live ranges.
    std::vector<unsigned> mappings;
    unsigned char byte=0;
    for(unsigned i=0;i<1024;++i) {
        const auto range=bitmap_store.Map(&byte,1);
        CHECK(range);
        mappings.push_back(range);
    }
    const BitmapProviderSnapshot mapped_full;
    CHECK(!bitmap_store.Map(&byte,0xffffffffu) && BitmapProviderSnapshot()==mapped_full);
    const auto hole=mappings[mappings.size()/2];
    bitmap_store.Unmap(hole);
    const auto reused=bitmap_store.Map(&byte,1);
    CHECK(reused==hole && bitmap_store.ranges[reused-1].base);
    {
        const auto &fresh=bitmap_store.ranges[reused-1];
        bool overlap=false;
        for(unsigned i=0;i<bitmap_store.range_capacity;++i)
        {
            const auto &r=bitmap_store.ranges[i];
            if(i!=reused-1 && r.base && fresh.base<r.base+r.size && r.base<fresh.base+fresh.size) overlap=true;
        }
        CHECK(!overlap);
    }
    for(auto range:mappings) bitmap_store.Unmap(range);
    CHECK(BitmapProviderSnapshot()==one);

    // Fill a fresh provider's address space exactly, without backing allocation.
    // Unlike the old fixed descriptor limit, this is a real mapping limit.
    {
        BitmapStore space;
        const auto whole=space.Map(&byte,0xfffffff0u-0x10000u);
        CHECK(whole && !space.Map(&byte,1));
        space.Unmap(whole);
        const auto small=space.Map(&byte,1);
        CHECK(small==whole && space.Read8(space.ranges[small-1].base)==byte);
        space.Unmap(small);
    }
    CHECK(BitmapProviderSnapshot()==one);

    // Consume the actual backing budget. No giant bitmap transfer is needed;
    // the native quota and its cleanup are the behavior under test here.
    auto reserve=bitmap_store.GlobalAllocate(0,BitmapStore::Limit-bitmap_store.allocated);
    CHECK(reserve && bitmap_store.allocated==BitmapStore::Limit);
    const BitmapProviderSnapshot quota;
    CHECK(!m.Call("CreateBitmap",8,8,1,1,0) && m.error==87 && BitmapProviderSnapshot()==quota);
    bitmap_store.GlobalFree(reserve);
    CHECK(BitmapProviderSnapshot()==one);
    // The segmented backing fits but its additional linear drawing mirror
    // does not. Attach must free both the DDB and its original global blocks.
    reserve=bitmap_store.GlobalAllocate(0,BitmapStore::Limit-bitmap_store.allocated-110000);
    CHECK(reserve);
    const BitmapProviderSnapshot mirror_quota;
    CHECK(!m.Call("CreateBitmap",240,3000,1,1,0) && m.error==8 && BitmapProviderSnapshot()==mirror_quota);
    bitmap_store.GlobalFree(reserve);
    CHECK(BitmapProviderSnapshot()==one);

    const auto pattern=CreatePatternBrush(bitmap);
    CHECK(pattern && DeleteObject(bitmap) && !ObjectExists(bitmap));
    CHECK(DeleteObject(pattern));
    ShutdownBitmaps();
    CHECK(BitmapProviderSnapshot()==empty);
    // Reinitialize after all previous failures and after stock teardown.
    const auto again=m.Call("CreateBitmap",8,8,1,1,0);
    CHECK(again && DeleteObject(again));
    ShutdownBitmaps();
    CHECK(BitmapProviderSnapshot()==empty);
    std::puts("BITMAP PROVIDER EDGES PASS: storage OOM, short buffers, object/range/quota exhaustion, pattern rollback and teardown");
}
