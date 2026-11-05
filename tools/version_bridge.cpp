// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_kernel.h"
extern "C" unsigned version(unsigned flags) { return Win98::Version(flags); }

extern "C" unsigned handle_count(unsigned requested) { return Win98::HandleCount(requested); }
extern "C" unsigned process_version(unsigned pid, unsigned selector, unsigned packed, unsigned missing,
                                    unsigned *out)
{
    struct Environment
    {
        unsigned selector, packed, missing, *out, count = 0;
        void Event(unsigned kind, unsigned value = 0) { out[2 + count++] = kind; out[2 + count++] = value; }
        void Lock() { Event(1); }
        void Unlock() { Event(2); }
        unsigned CurrentProcess() { return 1; }
        unsigned FindProcess(unsigned pid) { Event(3, pid); return pid == 1 ? 1 : 0; }
        unsigned MainModule(unsigned) { return selector; }
        unsigned ModuleVersion(unsigned pointer) { Event(4, pointer); return missing ? 0 : packed; }
        void Error(unsigned value) { out[0] = value; }
    } e{selector, packed, missing, out};
    const auto result = Win98::ProcessVersion(e, pid);
    out[1] = e.count;
    return result;
}
