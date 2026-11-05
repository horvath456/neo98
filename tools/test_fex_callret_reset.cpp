// SPDX-License-Identifier: GPL-2.0-or-later
#include <FEXCore/Utils/AllocatorHooks.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
int main() {
    const size_t page = getpagesize(), bytes = 4 * 1024 * 1024;
    auto *mapping = static_cast<unsigned char *>(::mmap(nullptr, bytes + 2 * page,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
    assert(mapping != MAP_FAILED);
    auto *stack = mapping + page;
    mapping[page - 1] = 0xa5;
    stack[bytes] = 0x5a;
    for (unsigned round = 0; round < 3; ++round) {
        memset(stack, 0xcc, bytes);
        FEXCore::Allocator::VirtualResetCallRet(stack, bytes);
        std::vector<char> resident(bytes / page);
        assert(!mincore(stack, bytes, resident.data()));
        unsigned present = 0;
        for (char value : resident) present += (value & MINCORE_INCORE) != 0;
        assert(present < resident.size() / 2); // resetting must not touch all pages
        for (size_t i = 0; i < bytes; ++i) assert(!stack[i]);
        assert(mapping[page - 1] == 0xa5 && stack[bytes] == 0x5a);
        printf("PASS callret reset round=%u zero=%zu resident=%u neighbours=intact\n", round, bytes, present);
    }
    assert(!::munmap(mapping, bytes + 2 * page));
}
