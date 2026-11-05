// SPDX-License-Identifier: GPL-2.0-or-later
#include "direct_bridge.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <unistd.h>
#include <asm/hwcap2.h>
int main()
{
    assert(getauxval(AT_HWCAP2) & HWCAP2_FSGSBASE);
    auto *low = static_cast<unsigned char *>(mmap(reinterpret_cast<void *>(0x10000), 0x30000,
                                                  PROT_READ | PROT_WRITE | PROT_EXEC,
                                                  MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED_NOREPLACE, -1, 0));
    assert(low != MAP_FAILED);
    const size_t length = neo98_direct_gate_end - neo98_direct_gate_start;
    memcpy(low + 0x10000, neo98_direct_gate_start, length);
    const auto finish = reinterpret_cast<uint64_t>(neo98_direct_finish);
    memcpy(low + 0x10000 + length - 10, &finish, 8);
    const unsigned char code[] = {0xb8, 98, 0, 0, 0, 0xb9, 0x34, 0x12, 0, 0, 0x9a, 0, 0, 2, 0, 0x33, 0};
    memcpy(low, code, sizeof code);
    Neo98DirectFrame frame;
    frame.ip = 0x10000;
    frame.flags = 0x202;
    frame.gpr[4] = 0x3fff0;
    frame.segments[1] = 0x23;
    frame.segments[2] = 0x2b;
    frame.guest_fp.cwd = 0x37f;
    frame.guest_fp.mxcsr = 0x1f80;
    neo98_direct_enter(&frame);
    printf("DIRECT BRIDGE eax=%llu ecx=%llu esp=%llx event=%llu pid=%ld\n", (unsigned long long)frame.gpr[0],
           (unsigned long long)frame.gpr[1], (unsigned long long)frame.gpr[4],
           (unsigned long long)frame.event, long(getpid()));
    auto *stack = reinterpret_cast<unsigned *>(frame.gpr[4]);
    for (unsigned i = 0; i < 2; ++i)
        printf("stack[%u]=%08x\n", i, stack[i]);
    assert(frame.gpr[0] == 98 && frame.gpr[1] == 0x1234 && frame.event == 1);
    assert(frame.gpr[4] == 0x3ffe8 && stack[0] == 0x10011 && stack[1] == 0x23);
    assert(munmap(low, 0x30000) == 0);
}
