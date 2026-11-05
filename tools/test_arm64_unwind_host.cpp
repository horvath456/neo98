// SPDX-License-Identifier: GPL-2.0-or-later
// The unwinder of src/arm64_unwind.cpp against the code it describes.
//
// For every function of an ARM64 program on disk this runs the first
// instructions of the function in a small model of the processor (the
// instructions a prologue consists of: sub sp, stp/str of registers with the
// stack pointer as base, mov/add x29, the stack probe), stops after each one,
// and asks the unwinder to take the frame up from there. Whatever the
// function had done so far, the answer must be the state of its caller: the
// stack pointer and the return address of the entry, and x19..x29 and
// d8..d15 as they were. The model knows instructions, the unwinder knows
// unwind codes and packed entries; neither sees the other's source of truth.
// Epilogues are not walked. tools/test_arm64_unwind_host.py builds the
// programs and this tool. docs/ARM64-APPS.md, "Exceptions".
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>
#include "../src/arm64_unwind.h"

using namespace Arm64::Unwind;

namespace
{
constexpr U64 Base = 0x00400000, StackTop = 0x10000000, StackBytes = 0x400000, Return = 0x00123454;

struct Program final : Memory
{
    std::vector<unsigned char> image, stack;
    bool Read(U64 address, void *out, unsigned size) override
    {
        if (address >= Base && address + size <= Base + image.size())
        {
            memcpy(out, &image[address - Base], size);
            return true;
        }
        if (address >= StackTop - StackBytes && address + size <= StackTop)
        {
            memcpy(out, &stack[address - (StackTop - StackBytes)], size);
            return true;
        }
        return false;
    }
    bool Write(U64 address, const void *data, unsigned size)
    {
        if (address < StackTop - StackBytes || address + size > StackTop) return false;
        memcpy(&stack[address - (StackTop - StackBytes)], data, size);
        return true;
    }
    unsigned U32At(unsigned rva) const
    {
        unsigned value = 0;
        if (rva + 4 <= image.size()) memcpy(&value, &image[rva], 4);
        return value;
    }
};

bool Load(const char *path, Program &program)
{
    std::vector<unsigned char> file;
    FILE *in = fopen(path, "rb");
    if (!in) return false;
    for (int c; (c = fgetc(in)) != EOF;) file.push_back((unsigned char)c);
    fclose(in);
    auto u32 = [&](size_t at) { unsigned v = 0; if (at + 4 <= file.size()) memcpy(&v, &file[at], 4); return v; };
    auto u16 = [&](size_t at) { unsigned short v = 0; if (at + 2 <= file.size()) memcpy(&v, &file[at], 2); return v; };
    if (file.size() < 0x100 || u16(0) != 0x5a4d) return false;
    const unsigned pe = u32(0x3c);
    if (u32(pe) != 0x4550 || u16(pe + 4) != 0xaa64) return false;
    const unsigned sections = u16(pe + 6), optional = u16(pe + 20), size = u32(pe + 24 + 56), headers = u32(pe + 24 + 60);
    program.image.assign(size, 0);
    memcpy(program.image.data(), file.data(), headers < file.size() ? headers : file.size());
    for (unsigned i = 0; i < sections; ++i)
    {
        const unsigned at = pe + 24 + optional + 40 * i;
        const unsigned virtual_size = u32(at + 8), rva = u32(at + 12), raw_size = u32(at + 16), raw = u32(at + 20);
        const unsigned bytes = raw_size < virtual_size ? raw_size : virtual_size;
        if (rva + bytes <= size && raw + bytes <= file.size()) memcpy(&program.image[rva], &file[raw], bytes);
    }
    program.stack.assign(StackBytes, 0xcc);
    return true;
}

// The registers a prologue touches.
struct State
{
    U64 x[32]; // x[31] is the stack pointer
    U64 d[32];
};

// One instruction of a prologue. False for anything else: the model stops.
bool Step(Program &program, State &s, unsigned instruction)
{
    const unsigned rt = instruction & 31, rn = instruction >> 5 & 31;
    // sub sp, sp, #imm (lsl #12)
    if ((instruction & 0xff8003ff) == 0xd10003ff)
    {
        const U64 imm = instruction >> 10 & 0xfff;
        s.x[31] -= instruction & 0x00400000 ? imm << 12 : imm;
        return true;
    }
    // mov x29, sp; add x29, sp, #imm
    if ((instruction & 0xffc003ff) == 0x910003fd)
    {
        s.x[29] = s.x[31] + (instruction >> 10 & 0xfff);
        return true;
    }
    // stp of x registers or d registers, base sp: offset or pre-index
    if ((instruction & 0x7fc00000) == 0x29000000 || (instruction & 0x7fc00000) == 0x29800000 ||
        (instruction & 0xffc00000) == 0x6d000000 || (instruction & 0xffc00000) == 0x6d800000)
    {
        const bool vector = instruction & 0x04000000, pre = instruction & 0x00800000;
        if (rn != 31 || (!vector && !(instruction & 0x80000000))) return false;
        const unsigned rt2 = instruction >> 10 & 31;
        long long offset = (long long)((instruction >> 15 & 0x7f) ^ 0x40) - 0x40;
        offset *= 8;
        const U64 address = s.x[31] + (U64)offset;
        const U64 first = vector ? s.d[rt] : rt == 31 ? 0 : s.x[rt], second = vector ? s.d[rt2] : rt2 == 31 ? 0 : s.x[rt2];
        if (!program.Write(address, &first, 8) || !program.Write(address + 8, &second, 8)) return false;
        if (pre) s.x[31] = address;
        return true;
    }
    // str x/d, [sp, #imm] (unsigned offset)
    if ((instruction & 0xffc00000) == 0xf9000000 || (instruction & 0xffc00000) == 0xfd000000)
    {
        if (rn != 31) return false;
        const bool vector = instruction & 0x04000000;
        const U64 address = s.x[31] + 8ull * (instruction >> 10 & 0xfff), value = vector ? s.d[rt] : rt == 31 ? 0 : s.x[rt];
        return program.Write(address, &value, 8);
    }
    // str x/d, [sp, #-imm]! (pre-index)
    if ((instruction & 0xffe00c00) == 0xf8000c00 || (instruction & 0xffe00c00) == 0xfc000c00)
    {
        if (rn != 31) return false;
        const bool vector = instruction & 0x04000000;
        const long long offset = (long long)((instruction >> 12 & 0x1ff) ^ 0x100) - 0x100;
        const U64 address = s.x[31] + (U64)offset, value = vector ? s.d[rt] : rt == 31 ? 0 : s.x[rt];
        if (!program.Write(address, &value, 8)) return false;
        s.x[31] = address;
        return true;
    }
    // The stack probe of a large frame: mov x15, #n; bl __chkstk; sub sp, sp, x15, lsl #4
    if ((instruction & 0xffe0001f) == 0xd280000f)
    {
        s.x[15] = instruction >> 5 & 0xffff;
        return true;
    }
    if ((instruction & 0xffe0001f) == 0xf2a0000f)
    {
        s.x[15] = (s.x[15] & 0xffff) | (U64)(instruction >> 5 & 0xffff) << 16;
        return true;
    }
    if ((instruction & 0xfc000000) == 0x94000000) return true; // bl: the probe changes nothing a caller sees
    if (instruction == 0xcb2f73ff)
    {
        s.x[31] -= s.x[15] << 4;
        return true;
    }
    return false;
}
} // namespace

int main(int argc, char **argv)
{
    unsigned long long functions = 0, points = 0, packed = 0, with_handler = 0, failures = 0, leaf_like = 0;
    for (int file = 1; file < argc; ++file)
    {
        Program program;
        Image image;
        if (!Load(argv[file], program) || !ReadImage(program, Base, image))
        {
            printf("FAIL %s: no ARM64 image\n", argv[file]);
            return 1;
        }
        for (unsigned entry = 0; entry + 8 <= image.table_bytes; entry += 8)
        {
            RuntimeFunction function;
            program.Read(Base + image.table + entry, &function, sizeof function);
            RuntimeFunction found;
            U64 found_at = 0;
            // The entry is found by an address inside its function.
            if (!Lookup(program, image, Base + function.begin, found, found_at) || found_at != Base + image.table + entry)
            {
                if (failures++ < 10) printf("FAIL %s: function %08x is not found by its own address\n", argv[file], function.begin);
                continue;
            }
            ++functions;
            if (function.unwind & 3) ++packed;
            State state;
            for (unsigned i = 0; i < 32; ++i)
            {
                state.x[i] = 0x1000000000000000ull + 0x0101010101010101ull * i + function.begin;
                state.d[i] = 0x2000000000000000ull + 0x0101010101010101ull * i + function.begin;
            }
            const U64 entry_sp = StackTop - 0x1000;
            state.x[31] = entry_sp;
            state.x[30] = Return;
            const State before = state;
            // A fragment of a function (a funclet or a split part) begins
            // with a frame that is not the caller's: nothing to compare.
            bool compared = false;
            for (unsigned k = 0; k < 64; ++k)
            {
                Context context = {};
                for (unsigned i = 0; i < 31; ++i) context.x[i] = state.x[i];
                context.sp = state.x[31];
                context.pc = Base + function.begin + 4 * k;
                for (unsigned i = 0; i < 32; ++i) context.v[i].low = state.d[i];
                U64 handler = 0, data = 0, frame = 0;
                const Result result = VirtualUnwind(program, Base, context.pc, &function, context, handler, data, frame);
                bool same = result == Ok && context.sp == entry_sp && context.pc == Return && frame == entry_sp;
                for (unsigned i = 19; i < 30 && same; ++i) same = context.x[i] == before.x[i];
                for (unsigned i = 8; i < 16 && same; ++i) same = context.v[i].low == before.d[i];
                if (handler) ++with_handler;
                ++points;
                compared = true;
                if (!same && failures++ < 10)
                    printf("FAIL %s: function %08x after %u instructions: result %u sp %llx (entry %llx) pc %llx\n", argv[file],
                           function.begin, k, (unsigned)result, context.sp, entry_sp, context.pc);
                if (!same) break;
                const unsigned instruction = program.U32At(function.begin + 4 * k);
                if (!Step(program, state, instruction)) break;
            }
            if (!compared) ++leaf_like;
        }
    }
    printf("UNWIND functions=%llu points=%llu packed=%llu handlers=%llu failures=%llu\n", functions, points, packed, with_handler,
           failures);
    return failures ? 1 : 0;
}
