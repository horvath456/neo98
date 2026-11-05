// SPDX-License-Identifier: GPL-2.0-or-later
#include "dosbox_pure_dynrec_unwind.h"
#include "dosbox_pure_resume.h"
#include <sys/mman.h>
#include <pthread.h>
#include <libkern/OSCacheControl.h>
#include <cstdio>
#include <cstring>

static int depth;
static bool destroyed;
using Generated = void (*)(void (*)());
static Generated generated;
struct Probe { ~Probe() { destroyed = true; } };
__attribute__((noinline)) static void NativeStop() {
    Probe probe;
    Neo98NestedRun count(depth);
    throw Neo98DosShutdown();
}
__attribute__((noinline)) static void NativeNestedStop() {
    Neo98NestedRun count(depth);
    generated(NativeStop);
}
__attribute__((noinline)) static void NativeReturn() {
    Neo98NestedRun count(depth);
}

int main(int argc, char**) {
    void* code = mmap(nullptr, 16384, PROT_READ|PROT_WRITE|PROT_EXEC,
                      MAP_PRIVATE|MAP_ANON|MAP_JIT, -1, 0);
    if (code == MAP_FAILED) return 2;
    // Same saved-register frame and native BLR as risc_armv8le.h.
    const uint32_t instructions[] = {
        0xa9bd7bfd, 0x910003fd, 0xa90153f3, 0xa9025bf5,
        0xaa0003f0, 0xd63f0200, 0xa94153f3, 0xa9425bf5,
        0xa8c37bfd, 0xd65f03c0
    };
    pthread_jit_write_protect_np(0);
    memcpy(code, instructions, sizeof(instructions));
    sys_icache_invalidate(code, sizeof(instructions));
    pthread_jit_write_protect_np(1);
    Neo98DynrecUnwind unwind;
    generated = reinterpret_cast<Generated>(code);
    if (argc == 1) unwind.Set(static_cast<char*>(code) + 16, sizeof(instructions) - 16);
    bool caught = false;
    try { generated(NativeStop); }
    catch (const Neo98DosShutdown&) { caught = true; }
    bool normal = true, nested = true;
    for (unsigned i = 0; i < 32; ++i) {
        generated(NativeReturn);
        normal &= depth == 0;
        caught = destroyed = false;
        try { generated(NativeNestedStop); }
        catch (const Neo98DosShutdown&) { caught = true; }
        nested &= caught && destroyed && depth == 0;
        unwind.Reset();
        unwind.Set(static_cast<char*>(code) + 16, sizeof(instructions) - 16);
    }
    unwind.Reset();
    munmap(code, 16384);
    const bool okay = caught && destroyed && depth == 0 && normal && nested;
    printf("%s typed DOS shutdown crosses ARM64 generated code; native destructors restore depth\n", okay ? "ok" : "FAIL");
    printf("%s normal native returns survive registered generated code\n", normal ? "ok" : "FAIL");
    printf("%s nested generated frames unwind after 32 register/deregister cycles\n", nested ? "ok" : "FAIL");
    return okay ? 0 : 1;
}
