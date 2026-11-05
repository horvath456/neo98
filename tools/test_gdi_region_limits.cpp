// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <new>
static bool fail_malloc,fail_new;
static void *checked_malloc(size_t size){return fail_malloc?nullptr:std::malloc(size);}
void *operator new[](std::size_t size,const std::nothrow_t &) noexcept {
    if(fail_new)return nullptr;try{return ::operator new[](size);}catch(...){return nullptr;}
}
#define malloc checked_malloc
#include "../src/gdi.cpp"
#undef malloc
namespace GdiTrueType {bool AbcWidths(const Font &,unsigned,unsigned,int *){std::abort();}}
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::abort();}} while(0)
int main(){
    using namespace Gdi;
    // Cross both the old region count and the shared 8192-range mapping cap.
    unsigned regions[9000]{};
    for(unsigned i=0;i<9000;++i){regions[i]=CreateRegionRect(i,0,i+1,1);CHECK(regions[i]);}
    for(unsigned i=0;i<9000;++i){CHECK(RegionContainsPoint(regions[i],i,0));CHECK(!RegionContainsPoint(regions[i],i+1,0));}
    for(auto h:regions)CHECK(DeleteObject(h));
    ShutdownBitmaps();
    fail_new=true;CHECK(!region_store.Allocate(42));fail_new=false;
    const unsigned bytes=(4u<<20)+64;
    const auto raw=region_store.Allocate(bytes);CHECK(raw);
    const auto address=region_store.Pointer(raw);
    region_store.Write32(address,0x12345678);region_store.Write32(address+bytes-4,0x87654321);
    fail_malloc=true;CHECK(!region_store.Reallocate(raw,bytes*2));fail_malloc=false;
    CHECK(region_store.Pointer(raw)==address&&region_store.Size(raw)==bytes);
    CHECK(region_store.Read32(address)==0x12345678&&region_store.Read32(address+bytes-4)==0x87654321);
    CHECK(region_store.Reallocate(raw,bytes*2)==raw);
    const auto moved=region_store.Pointer(raw);
    CHECK(region_store.Read32(moved)==0x12345678&&region_store.Read32(moved+bytes-4)==0x87654321);
    CHECK(region_store.Read32(moved+bytes*2-4)==0);
    CHECK(!region_store.Free(raw));CHECK(!region_store.Pointer(raw));
    // A failed mapping expansion leaves current address caches and data valid.
    unsigned mappings[20000]{},n=0;unsigned char byte=17;
    while(n<bitmap_store.range_capacity){mappings[n]=bitmap_store.Map(&byte,1);CHECK(mappings[n]);++n;}
    auto first=bitmap_store.ranges[mappings[0]-1].base;CHECK(bitmap_store.Read8(first)==17);
    fail_new=true;CHECK(!bitmap_store.Map(&byte,1));fail_new=false;
    CHECK(bitmap_store.Read8(first)==17);
    const auto extra=bitmap_store.Map(&byte,1);CHECK(extra);CHECK(bitmap_store.Read8(first)==17);
    bitmap_store.Unmap(extra);for(unsigned i=0;i<n;++i)bitmap_store.Unmap(mappings[i]);
    ShutdownBitmaps();CHECK(!LiveObjects());
    std::printf("GDI region limits PASS: %u checks, ASan/UBSan\n",checks);
}
