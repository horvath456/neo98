// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/session_dib_memory.h"
#include <assert.h>
#include <stdio.h>

int main()
{
    void *storage = nullptr;
    assert(!SessionDibMemory::Allocate(0, storage) && !storage);
    assert(!SessionDibMemory::Allocate(4096, storage) && !storage);
    const unsigned sizes[] = {1, 2, 3, 17, 1024};
    for (unsigned pages : sizes)
    {
        const unsigned bytes = pages * SessionDibMemory::Alignment;
        auto *bits = SessionDibMemory::Allocate(bytes, storage);
        assert(bits && storage);
        assert(!(reinterpret_cast<uintptr_t>(bits) % SessionDibMemory::Alignment));
        assert(bits >= static_cast<unsigned char *>(storage));
        assert(size_t(bits - static_cast<unsigned char *>(storage)) < SessionDibMemory::Alignment);
        for (unsigned i = 0; i < bytes; ++i) assert(bits[i] == 0);
        memset(bits, 0xa5, bytes);
        assert(bits[0] == 0xa5 && bits[bytes - 1] == 0xa5);
        free(storage);
    }
    puts("session DIB alignment, zeroing, bounds and release PASS");
}
