// SPDX-License-Identifier: GPL-2.0-or-later
#include "fiber.h"
#include <stdlib.h>
#ifndef __has_feature
#define __has_feature(x) 0
#endif
#if __has_feature(address_sanitizer)
#include <sanitizer/common_interface_defs.h>
#define FIBER_ASAN 1
#endif
extern "C" void neo98_switch_context(void *, void *);
extern "C" void neo98_fiber_entry();
extern "C" void neo98_fiber_run(Fiber *fiber)
{
    fiber->Run();
}
Fiber::~Fiber()
{
    free(stack);
}
bool Fiber::Start(Entry function, void *data, unsigned stack_bytes)
{
    if (stack || !function || stack_bytes < 16384 || stack_bytes > ~0u - 15)
        return false;
    size = (stack_bytes + 15) & ~15u;
    stack = static_cast<unsigned char *>(malloc(size));
    if (!stack)
        return false;
    entry = function;
    argument = data;
    callee.words[0] = (unsigned long long)this;                     // x19 or RBX, passed to the entry trampoline
    callee.words[11] = (unsigned long long)neo98_fiber_entry;       // LR
    callee.words[12] = ((unsigned long long)stack + size) & ~15ull; // SP
    // Inherit floating-point control and status, including the host's rounding mode.
#if defined(__x86_64__)
    // SysV function entry has an eight-byte return slot below a 16-byte boundary.
    callee.words[12] -= 8;
    asm volatile("stmxcsr %0" : "=m"(callee.words[22]));
    asm volatile("fnstcw %0" : "=m"(callee.words[23]));
#else
    asm volatile("mrs %0, fpcr" : "=r"(callee.words[22]));
    asm volatile("mrs %0, fpsr" : "=r"(callee.words[23]));
#endif
    return true;
}
void Fiber::Resume()
{
    if (!stack || finished || active)
        return;
    active = true;
    started = true;
#ifdef FIBER_ASAN
    __sanitizer_start_switch_fiber(&caller_fake, stack, size);
#endif
    neo98_switch_context(&caller, &callee);
#ifdef FIBER_ASAN
    __sanitizer_finish_switch_fiber(caller_fake, nullptr, nullptr);
#endif
    active = false;
}
void Fiber::Yield()
{
    if (!active)
        return;
#ifdef FIBER_ASAN
    __sanitizer_start_switch_fiber(finished ? nullptr : &callee_fake, caller_bottom, caller_size);
#endif
    neo98_switch_context(&callee, &caller);
#ifdef FIBER_ASAN
    __sanitizer_finish_switch_fiber(callee_fake, nullptr, nullptr);
#endif
}
void Fiber::Run()
{
#ifdef FIBER_ASAN
    __sanitizer_finish_switch_fiber(nullptr, &caller_bottom, &caller_size);
#endif
    entry(argument);
    finished = true;
    Yield();
    __builtin_trap(); // A completed execution context cannot be resumed.
}
