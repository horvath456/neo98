// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/linux_shared_memory.h"
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <thread>
extern "C" size_t neo98_state_allocation_bytes(const void *);
int main()
{
    void *p=malloc(8); assert(p && neo98_state_allocation_bytes(p)==8);
    assert(neo98_state_allocation_bytes(static_cast<char *>(p)+1)==0);
    memset(p,0x98,8); p=realloc(p,48); assert(p && neo98_state_allocation_bytes(p)==48);
    assert(static_cast<unsigned char *>(p)[7]==0x98);
    errno=EDOM; free(p); assert(errno==EDOM && !neo98_state_allocation_bytes(p));
    p=calloc(3,7); assert(p && neo98_state_allocation_bytes(p)==21); free(p);
    void *aligned=nullptr; errno=ERANGE;
    assert(!posix_memalign(&aligned,4096,4096) && errno==ERANGE);
    assert(neo98_state_allocation_bytes(aligned)==4096); free(aligned);
    auto *pages=static_cast<unsigned char *>(LinuxSharedMemory::Allocate(4096)); assert(pages);
    memset(pages,0x98,4096); p=realloc(pages,8192);
    assert(p && neo98_state_allocation_bytes(p)==8192 && static_cast<unsigned char *>(p)[4095]==0x98);
    assert(LinuxSharedMemory::blocks.empty()); free(p);
    pages=static_cast<unsigned char *>(LinuxSharedMemory::Allocate(4096)); assert(pages);
    free(pages); assert(LinuxSharedMemory::blocks.empty());
    auto allocate=[] {
        for (unsigned i=0;i<10000;++i)
        { void *p=malloc(19); assert(p); p=realloc(p,79); assert(p && neo98_state_allocation_bytes(p)==79); free(p); }
    };
    std::thread a(allocate),b(allocate); a.join(); b.join();
    puts("LINUX STATE ALLOCATOR PASS: exact requested bytes, alignment, page arrays, realloc, concurrent lifetime");
}
