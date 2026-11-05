// SPDX-License-Identifier: GPL-2.0-or-later
// One frame of an ARM64 program, unwound from.pdata and.xdata. The format
// is the one Microsoft documents for Windows on ARM64 ("ARM64 exception
// handling"): an entry of eight bytes per function, either with the packed
// description of a canonical prologue or with the address of a record that
// lists unwind codes, one per instruction of the prologue, in the order that
// undoes them. src/arm64_unwind.h; docs/ARM64-APPS.md, "Exceptions".
#include "arm64_unwind.h"
#include <string.h>

namespace Arm64
{
namespace Unwind
{
namespace
{
bool Read32(Memory &memory, U64 address, U32 &value) { return memory.Read(address, &value, 4); }
bool Read16(Memory &memory, U64 address, unsigned short &value) { return memory.Read(address, &value, 2); }

// Bytes one unwind code occupies.
unsigned CodeBytes(unsigned char code)
{
    if (code < 0xc0) return 1;
    if (code < 0xe0) return 2;
    if (code == 0xe0) return 4;
    if (code == 0xe2) return 2;
    if (code == 0xe7) return 3;
    return 1;
}
bool EndCode(unsigned char code) { return code == 0xe4 || code == 0xe5; }
// Instructions a sequence of codes stands for, up to its end code. The codes
// of the custom frames (E8h..EFh) describe no instruction.
unsigned Instructions(const unsigned char *code, const unsigned char *end)
{
    unsigned count = 0;
    while (code < end && !EndCode(*code))
    {
        if ((*code & 0xf8) != 0xe8) ++count;
        code += CodeBytes(*code);
    }
    return count;
}

struct Frame
{
    Memory &memory;
    Context &context;
    // The return address is what the codes left in Lr; a machine frame or a
    // context on the stack names the pc itself.
    bool pc_from_lr = true;
    Result result = Ok;

    bool Slot(U64 address, U64 &value)
    {
        if (memory.Read(address, &value, 8)) return true;
        result = BadMemory;
        return false;
    }
    // `count` registers saved from `position` slots above the stack pointer;
    // a negative position is a save that moved the stack pointer down by that
    // many slots first (stp..., [sp, #-n]!).
    void Integers(unsigned first, unsigned count, int position)
    {
        const unsigned offset = position > 0 ? (unsigned)position : 0;
        for (unsigned i = 0; i < count && first + i < 31; ++i)
            if (!Slot(context.sp + 8ull * (offset + i), context.x[first + i])) return;
        if (position < 0) context.sp += 8ull * (unsigned)-position;
    }
    void Floats(unsigned first, unsigned count, int position)
    {
        const unsigned offset = position > 0 ? (unsigned)position : 0;
        for (unsigned i = 0; i < count && first + i < 32; ++i)
            if (!Slot(context.sp + 8ull * (offset + i), context.v[first + i].low)) return;
        if (position < 0) context.sp += 8ull * (unsigned)-position;
    }
    // save_any_reg: register, pair, pre-indexed, kind (x, d, q) and offset.
    void Any(unsigned char first, unsigned char second, unsigned pair_count)
    {
        const unsigned reg = first & 0x1f, kind = second >> 6, raw = second & 0x3f;
        const bool pair = first & 0x40, indexed = first & 0x20;
        const unsigned count = pair ? pair_count : 1;
        const unsigned bytes = indexed || kind == 2 ? raw * 16 : raw * 8;
        const U64 base = indexed ? context.sp : context.sp + bytes;
        for (unsigned i = 0; i < count; ++i)
        {
            if (kind == 0)
            {
                if (reg + i < 31 && !Slot(base + 8ull * i, context.x[reg + i])) return;
                if (reg + i == 31 && !Slot(base + 8ull * i, context.sp)) return;
            }
            else if (kind == 1)
            {
                if (reg + i < 32 && !Slot(base + 8ull * i, context.v[reg + i].low)) return;
            }
            else if (kind == 2)
            {
                if (reg + i < 32 && (!Slot(base + 16ull * i, context.v[reg + i].low) ||
                                     !Slot(base + 16ull * i + 8, context.v[reg + i].high)))
                    return;
            }
            else
            {
                result = BadCode;
                return;
            }
        }
        if (indexed) context.sp += bytes;
    }

    // Undoes what the codes describe, after leaving out the first `skip` of
    // them: the instructions of the prologue that have not run yet, or the
    // ones of the epilogue that already have.
    void Run(const unsigned char *code, const unsigned char *end, unsigned skip)
    {
        for (; code < end && skip && !EndCode(*code); --skip) code += CodeBytes(*code);
        // save_next extends the pair that follows it by one more pair.
        unsigned pair = 2;
        while (code < end && result == Ok)
        {
            const unsigned char c = *code;
            const unsigned bytes = CodeBytes(c);
            if (code + bytes > end) break;
            const unsigned value = bytes > 1 ? (unsigned)c << 8 | code[1] : c;
            if (c < 0x20) context.sp += 16ull * (value & 0x1f);                                // alloc_s
            else if (c < 0x40) Integers(19, pair, -(int)(value & 0x1f));                       // save_r19r20_x
            else if (c < 0x80) Integers(29, 2, (int)(value & 0x3f));                           // save_fplr
            else if (c < 0xc0) Integers(29, 2, -(int)(value & 0x3f) - 1);                      // save_fplr_x
            else if (c < 0xc8) context.sp += 16ull * (value & 0x7ff);                          // alloc_m
            else if (c < 0xcc) Integers(19 + ((value >> 6) & 0xf), pair, (int)(value & 0x3f)); // save_regp
            else if (c < 0xd0) Integers(19 + ((value >> 6) & 0xf), pair, -(int)(value & 0x3f) - 1); // save_regp_x
            else if (c < 0xd4) Integers(19 + ((value >> 6) & 0xf), 1, (int)(value & 0x3f));    // save_reg
            else if (c < 0xd6) Integers(19 + ((value >> 5) & 0xf), 1, -(int)(value & 0x1f) - 1); // save_reg_x
            else if (c < 0xd8)                                                                 // save_lrpair
            {
                Integers(19 + 2 * ((value >> 6) & 0x7), 1, (int)(value & 0x3f));
                Integers(30, 1, (int)(value & 0x3f) + 1);
            }
            else if (c < 0xda) Floats(8 + ((value >> 6) & 0x7), pair, (int)(value & 0x3f));    // save_fregp
            else if (c < 0xdc) Floats(8 + ((value >> 6) & 0x7), pair, -(int)(value & 0x3f) - 1); // save_fregp_x
            else if (c < 0xde) Floats(8 + ((value >> 6) & 0x7), 1, (int)(value & 0x3f));       // save_freg
            else if (c == 0xde) Floats(8 + ((value >> 5) & 0x7), 1, -(int)(value & 0x1f) - 1); // save_freg_x
            else if (c == 0xe0) context.sp += 16ull * ((U64)code[1] << 16 | (U64)code[2] << 8 | code[3]); // alloc_l
            else if (c == 0xe1) context.sp = context.x[29];                                    // set_fp
            else if (c == 0xe2) context.sp = context.x[29] - 8ull * code[1];                   // add_fp
            else if (c == 0xe3) {}                                                             // nop
            else if (c == 0xe4) break;                                                         // end
            else if (c == 0xe5) {}                                                             // end_c
            else if (c == 0xe6)                                                                // save_next
            {
                pair += 2;
                code += bytes;
                continue;
            }
            else if (c == 0xe7) Any(code[1], code[2], pair);                                   // save_any_reg
            else if (c == 0xe9)                                                                // machine frame
            {
                U64 sp = 0, pc = 0;
                if (!Slot(context.sp, sp) || !Slot(context.sp + 8, pc)) return;
                context.sp = sp;
                context.pc = pc;
                context.flags &= ~ContextUnwoundToCall;
                pc_from_lr = false;
            }
            else if (c == 0xea)                                                                // context
            {
                Context saved;
                if (!memory.Read(context.sp, &saved, sizeof saved))
                {
                    result = BadMemory;
                    return;
                }
                const U32 flags = (context.flags & ~ContextUnwoundToCall) | (saved.flags & ContextUnwoundToCall);
                context = saved;
                context.flags = flags;
                pc_from_lr = false;
            }
            else if (c == 0xec)                                                                // clear unwound to call
            {
                context.pc = context.x[30];
                context.flags &= ~ContextUnwoundToCall;
                pc_from_lr = false;
            }
            else if (c == 0xfc) {} // pac_sign_lr: a program here runs without pointer authentication
            else
            {
                result = BadCode;
                return;
            }
            pair = 2;
            code += bytes;
        }
    }
};

// The record behind an entry: the header, the epilogue scopes, the codes and
// the handler.
Result Full(Memory &memory, U64 base, U64 pc, const RuntimeFunction &function, Frame &frame, U64 &handler, U64 &handler_data)
{
    U64 at = base + function.unwind;
    U32 header = 0;
    if (!Read32(memory, at, header)) return BadMemory;
    at += 4;
    const U32 length = header & 0x3ffff;
    const bool has_handler = header >> 20 & 1, epilogue_in_header = header >> 21 & 1;
    U32 epilogues = header >> 22 & 0x1f, words = header >> 27 & 0x1f;
    if (!epilogues && !words)
    {
        U32 extension = 0;
        if (!Read32(memory, at, extension)) return BadMemory;
        at += 4;
        epilogues = extension & 0xffff;
        words = extension >> 16 & 0xff;
    }
    const U64 scopes = at;
    if (!epilogue_in_header) at += 4ull * epilogues;
    unsigned char codes[1024];
    const unsigned bytes = words * 4;
    if (bytes > sizeof codes) return BadCode;
    if (bytes && !memory.Read(at, codes, bytes)) return BadMemory;
    const unsigned char *const end = codes + bytes;
    const U32 offset = (U32)((pc - base - function.begin) / 4);

    // In the prologue: only what has run is undone.
    if (offset < bytes)
    {
        const unsigned count = Instructions(codes, end);
        if (offset < count)
        {
            frame.Run(codes, end, count - offset);
            return frame.result;
        }
    }
    // In an epilogue: what it has undone already is left out.
    if (!epilogue_in_header)
    {
        for (U32 i = 0; i < epilogues; ++i)
        {
            U32 scope = 0;
            if (!Read32(memory, scopes + 4ull * i, scope)) return BadMemory;
            const U32 start = scope & 0x3ffff, index = scope >> 22;
            if (offset < start) break;
            if (index < bytes && offset - start < bytes - index)
            {
                const unsigned count = Instructions(codes + index, end);
                if (offset <= start + count)
                {
                    frame.Run(codes + index, end, offset - start);
                    return frame.result;
                }
            }
        }
    }
    else if (epilogues < bytes && length - offset <= bytes - epilogues)
    {
        // One epilogue at the end of the function; the field is its first code.
        const unsigned count = Instructions(codes + epilogues, end) + 1;
        if (offset >= length - count)
        {
            frame.Run(codes + epilogues, end, offset - (length - count));
            return frame.result;
        }
    }
    frame.Run(codes, end, 0);
    if (frame.result != Ok) return frame.result;
    if (has_handler)
    {
        U32 rva = 0;
        if (!Read32(memory, at + bytes, rva)) return BadMemory;
        handler = rva ? base + rva : 0;
        handler_data = at + bytes + 4;
    }
    return Ok;
}

// The packed form: the entry itself says which registers the canonical
// prologue saves and how large the frame is. The codes of that prologue are
// written out and run like those of a record.
Result Packed(U64 base, U64 pc, const RuntimeFunction &function, Frame &frame)
{
    const U32 data = function.unwind;
    const unsigned flag = data & 3, length = data >> 2 & 0x7ff, reg_f = data >> 13 & 7, reg_i = data >> 16 & 0xf,
                   cr = data >> 21 & 3, frame_size = data >> 23 & 0x1ff;
    unsigned homes = data >> 20 & 1;
    if (flag == 3) return BadCode;
    unsigned integer_bytes = reg_i * 8, float_bytes = reg_f * 8;
    if (cr == 1) integer_bytes += 8;
    if (reg_f) float_bytes += 8;
    const unsigned saved_bytes = (integer_bytes + float_bytes + 64 * homes + 15) & ~15u;
    unsigned locals = frame_size * 16 - saved_bytes;
    const unsigned integer_slots = integer_bytes / 8, float_slots = float_bytes / 8, saved_slots = saved_bytes / 8;
    // Without saved registers the home area of the parameters is part of the
    // locals and one allocation covers both.
    if (homes && !reg_i && !reg_f && cr != 1)
    {
        locals += saved_bytes;
        homes = 0;
    }
    unsigned char prologue[48], epilogue[48];
    unsigned p = 0, e = 0;
    auto one = [&](unsigned code) { prologue[p++] = epilogue[e++] = (unsigned char)code; };
    auto two = [&](unsigned code) { one(code >> 8); one(code & 0xff); };
    if (cr == 2 || cr == 3)
    {
        one(0xe1);                                            // mov x29, sp
        if (locals <= 512) one(0x80 | (locals / 8 - 1));      // stp x29, lr, [sp, #-locals]!
        else one(0x40);                                       // stp x29, lr, [sp]
    }
    if ((cr <= 1 && locals) || locals > 512)
    {
        // alloc_s holds up to 31 units of 16 bytes; 512 bytes are alloc_m.
        if (locals < 512) one(locals / 16);                   // sub sp, #locals
        else if (locals <= 4080) two(0xc000 | locals / 16);
        else
        {
            // Two instructions: the rest, then 4080 bytes.
            if (locals - 4080 < 512) one((locals - 4080) / 16);
            else two(0xc000 | (locals - 4080) / 16);
            two(0xc000 | 4080 / 16);
        }
    }
    // Four instructions that store x0..x7; they change nothing to undo and
    // have no counterpart in the epilogue.
    if (homes)
        for (unsigned i = 0; i < 4; ++i) prologue[p++] = 0xe3;
    if (reg_f)
    {
        // d8..d(8+reg_f): pairs, and a single one when their number is odd.
        if (reg_f % 2 == 0) two(0xdc00 | reg_f << 6 | (integer_slots + float_slots - 1));
        for (int i = (int)(reg_f + 1) / 2 - 1; i >= 0; --i)
        {
            if (!i && !integer_bytes) two(0xda00 | (saved_slots - 1));
            else two(0xd800 | (unsigned)(2 * i) << 6 | (integer_slots + 2 * (unsigned)i));
        }
    }
    if (cr == 1 && reg_i % 2 == 0)
    {
        // Lr alone, behind the integer registers.
        if (!reg_i) two(0xd400 | (30 - 19) << 5 | (saved_slots - 1));
        else two(0xd000 | (30 - 19) << 6 | (integer_slots - 1));
    }
    if (reg_i)
    {
        if (reg_i % 2)
        {
            if (cr == 1)
            {
                two(0xd600 | ((reg_i - 1) / 2) << 6 | (integer_slots - 2)); // stp x(last), lr
                if (reg_i == 1) one(saved_slots / 2);
            }
            else if (reg_i == 1) two(0xd400 | (saved_slots - 1));           // str x19, [sp, #-saved]!
            else two(0xd000 | (integer_slots - 1) << 6 | (integer_slots - 1));
        }
        for (int i = (int)reg_i / 2 - 1; i >= 0; --i)
        {
            if (i) two(0xc800 | (unsigned)(2 * i) << 6 | (unsigned)(2 * i));
            else two(0xcc00 | (saved_slots - 1));                           // stp x19, x20, [sp, #-saved]!
        }
    }
    if (cr == 2) one(0xfc);
    prologue[p++] = 0xe4;
    epilogue[e++] = 0xe4;
    const U32 offset = (U32)((pc - base - function.begin) / 4);
    if (flag == 1)
    {
        unsigned count = Instructions(prologue, prologue + p);
        if (offset < count)
        {
            frame.Run(prologue, prologue + p, count - offset);
            return frame.result;
        }
        // The epilogue ends with the return.
        count = Instructions(epilogue, epilogue + e) + 1;
        if (length >= count && offset >= length - count)
        {
            frame.Run(epilogue, epilogue + e, offset - (length - count));
            return frame.result;
        }
    }
    frame.Run(prologue, prologue + p, 0);
    return frame.result;
}

bool FunctionLength(Memory &memory, U64 base, const RuntimeFunction &function, U32 &bytes)
{
    if (function.unwind & 3)
    {
        bytes = (function.unwind >> 2 & 0x7ff) * 4;
        return true;
    }
    U32 header = 0;
    if (!Read32(memory, base + function.unwind, header)) return false;
    bytes = (header & 0x3ffff) * 4;
    return true;
}
} // namespace

bool ReadImage(Memory &memory, U64 base, Image &image)
{
    unsigned short magic = 0, machine = 0, optional = 0;
    U32 header = 0, signature = 0, count = 0;
    if (!Read16(memory, base, magic) || magic != 0x5a4d || !Read32(memory, base + 0x3c, header)) return false;
    const U64 pe = base + header;
    if (!Read32(memory, pe, signature) || signature != 0x00004550) return false;
    if (!Read16(memory, pe + 4, machine) || machine != 0xaa64) return false;
    if (!Read16(memory, pe + 24, optional) || optional != 0x20b) return false;
    image = {};
    image.base = base;
    if (!Read32(memory, pe + 24 + 56, image.size) || !Read32(memory, pe + 24 + 108, count)) return false;
    // The exception directory is the fourth.
    if (count > 3 && (!Read32(memory, pe + 24 + 112 + 3 * 8, image.table) ||
                      !Read32(memory, pe + 24 + 112 + 3 * 8 + 4, image.table_bytes)))
        return false;
    return true;
}

bool Lookup(Memory &memory, const Image &image, U64 pc, RuntimeFunction &function, U64 &entry_address)
{
    if (!image.table || pc < image.base || pc - image.base >= image.size) return false;
    const U32 rva = (U32)(pc - image.base);
    // The entries are sorted by their begin.
    unsigned low = 0, high = image.table_bytes / 8;
    while (low < high)
    {
        const unsigned middle = low + (high - low) / 2;
        RuntimeFunction candidate;
        const U64 at = image.base + image.table + 8ull * middle;
        if (!memory.Read(at, &candidate, sizeof candidate)) return false;
        if (rva < candidate.begin)
        {
            high = middle;
            continue;
        }
        U32 bytes = 0;
        if (!FunctionLength(memory, image.base, candidate, bytes)) return false;
        if (rva < candidate.begin + bytes)
        {
            function = candidate;
            entry_address = at;
            return true;
        }
        low = middle + 1;
    }
    return false;
}

Result VirtualUnwind(Memory &memory, U64 base, U64 pc, const RuntimeFunction *function, Context &context, U64 &handler,
                     U64 &handler_data, U64 &frame)
{
    handler = handler_data = 0;
    if (!function)
    {
        // A leaf keeps its return address in Lr and has no frame of its own.
        if (pc == context.x[30]) return BadFunctionTable;
        context.flags |= ContextUnwoundToCall;
        context.pc = context.x[30];
        frame = context.sp;
        return Ok;
    }
    context.flags |= ContextUnwoundToCall;
    Frame state{memory, context};
    const Result result = function->unwind & 3 ? Packed(base, pc, *function, state)
                                               : Full(memory, base, pc, *function, state, handler, handler_data);
    if (result != Ok) return result;
    if (state.pc_from_lr) context.pc = context.x[30];
    frame = context.sp;
    return Ok;
}
} // namespace Unwind
} // namespace Arm64
