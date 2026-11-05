// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// AArch64 dynrec blocks all share gen_run_code's 48-byte frame. Native
// memory/FPU helpers may stop the guest while servicing a nested page fault.
// Describe that frame so a C++ stop can unwind through generated code and
// destroy its native callers, just as it does with the interpreter.
#if defined(__APPLE__) && defined(__aarch64__)
#include <unwind.h>
#include <cstdint>
#include <cstring>

class Neo98DynrecUnwind {
    alignas(8) unsigned char bytes[72] = {};
    const void* registered = nullptr;
    const void* code = nullptr;

    template<class T> void Write(unsigned offset, T value) {
        memcpy(bytes + offset, &value, sizeof(value));
    }
public:
    ~Neo98DynrecUnwind() { Reset(); }
    void Reset() {
        if (registered) __deregister_frame(registered);
        registered = code = nullptr;
    }
    void Set(const void* start, uint64_t size) {
        if (code == start) return;
        Reset();
        if (!start || !size) return;
        // DWARF CIE: zR, pcrel/sdata8; CFA=x29+48; x29,x30,x19..22
        // live at CFA-48,-40,-32,-24,-16,-8 respectively. The wrapper
        // itself has no native calls; this FDE covers only block code.
        const unsigned char cie[] = {
            28,0,0,0, 0,0,0,0, 1,'z','R',0, 1,0x78,30,1,0x1c,
            0x0c,29,48, 0x9d,6, 0x9e,5, 0x93,4, 0x94,3, 0x95,2, 0x96,1
        };
        memcpy(bytes, cie, sizeof(cie));
        Write<uint32_t>(32, 28); // FDE length, excluding the length word
        Write<uint32_t>(36, 36); // backward displacement to the CIE
        Write<int64_t>(40, reinterpret_cast<int64_t>(start) - reinterpret_cast<int64_t>(bytes + 40));
        Write<uint64_t>(48, size);
        memset(bytes + 56, 0, 16); // empty augmentation, padding, terminator
        registered = bytes + 32;
        code = start;
        __register_frame(registered);
    }
};
#else
class Neo98DynrecUnwind {
public:
    void Reset() {}
    void Set(const void*, unsigned long long) {}
};
#endif
