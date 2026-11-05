// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "x86_cpu.h"
#include "arm64_machine.h"

namespace Arm64Critical
{
constexpr unsigned Code[] = {
#include "arm64_critical_code.inc"
};
constexpr unsigned Bytes = 0x90, Commit = 0x6c, Slow = 0x80;
static_assert(sizeof Code == 2 * Bytes, "critical leaf layout");

// All guest threads and Windows providers run on the session's one scheduler
// thread. An HV interrupt may stop at *any* instruction; before exposing that
// stop to a callback or another thread, restart a read-only attempt or finish
// its three stores. No callback ever observes a partly published owner.
// The last Leave with a waiter enters the normal provider without stores, so
// that provider alone updates counters and wakes the next owner.
template<class Memory>
bool Interrupted(Arm64::Registers &r, unsigned start, Memory &memory)
{
    if (!start || r.pc < start || r.pc >= start + Slow) return true;
    const unsigned offset = (unsigned)(r.pc - start);
    if (offset <= Commit)
        r.pc = start;
    else
    {
        const unsigned values[] = {(unsigned)r.x[11], (unsigned)r.x[12]};
        const unsigned available = (unsigned)r.x[10];
        if (memory.Write(r.x[9] + 4, values, sizeof values) != X86::CpuError::Ok ||
            memory.Write(r.x[9] + 16, &available, sizeof available) != X86::CpuError::Ok) return false;
        r.pc = r.x[30];
    }
    return true;
}
}
