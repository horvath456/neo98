// SPDX-License-Identifier: GPL-2.0-or-later
// Unwinding one frame of an ARM64 program from the tables its linker wrote:
// the exception directory of a PE32+ image (.pdata) and the unwind records
// behind it (.xdata, or the packed form inside the entry). This is the part of
// RtlLookupFunctionEntry and RtlVirtualUnwind that is arithmetic over the
// memory of the program; who walks frames with it and calls handlers is
// src/arm64_seh.inc. Nothing here knows a machine or a host: the memory comes
// through an interface, so the unwinder is tested against images on disk
// (tools/test_arm64_unwind_host.py). docs/ARM64-APPS.md, "Exceptions".
#pragma once
namespace Arm64
{
namespace Unwind
{
typedef unsigned long long U64;
typedef unsigned U32;

// CONTEXT of Windows for ARM64 (ARM64_NT_CONTEXT), as a program sees it.
struct Context
{
    U32 flags, cpsr;
    U64 x[31]; // x[29] is Fp, x[30] is Lr
    U64 sp, pc;
    struct
    {
        U64 low, high;
    } v[32];
    U32 fpcr, fpsr;
    U32 bcr[8];
    U64 bvr[8];
    U32 wcr[2];
    U64 wvr[2];
};
static_assert(sizeof(Context) == 0x390, "CONTEXT of Windows for ARM64");
constexpr U32 ContextArm64 = 0x00400000, ContextControl = ContextArm64 | 1, ContextInteger = ContextArm64 | 2,
              ContextFloatingPoint = ContextArm64 | 4, ContextFull = ContextArm64 | 7,
              // The context stands behind a call (pc is a return address): the
              // instruction that belongs to it is the one before.
              ContextUnwoundToCall = 0x20000000;

// EXCEPTION_RECORD of Win64.
struct ExceptionRecord
{
    U32 code, flags;
    U64 record, address;
    U32 parameters, reserved;
    U64 information[15];
};
static_assert(sizeof(ExceptionRecord) == 0x98, "EXCEPTION_RECORD of Win64");
constexpr U32 ExceptionNoncontinuable = 1, ExceptionUnwinding = 2, ExceptionExitUnwind = 4, ExceptionStackInvalid = 8,
              ExceptionNestedCall = 0x10, ExceptionTargetUnwind = 0x20, ExceptionCollidedUnwind = 0x40;

// DISPATCHER_CONTEXT of Windows for ARM64: what a language handler gets
// beside the record.
struct DispatcherContext
{
    U64 control_pc, image_base, function_entry, establisher_frame, target_pc, context_record, language_handler,
        handler_data, history_table;
    U32 scope_index;
    unsigned char control_pc_is_unwound, reserved[3];
    // x19..x29 and d8..d15 of the frame, in this order.
    U64 nonvolatile_registers;
};
static_assert(sizeof(DispatcherContext) == 0x58, "DISPATCHER_CONTEXT of Windows for ARM64");
constexpr unsigned NonvolatileBytes = 11 * 8 + 8 * 8;

// One entry of the exception directory.
struct RuntimeFunction
{
    U32 begin, unwind;
};

class Memory
{
  public:
    virtual ~Memory() = default;
    virtual bool Read(U64 address, void *out, unsigned size) = 0;
};

// The exception directory of the image mapped at `base`.
struct Image
{
    U64 base = 0;
    U32 size = 0, table = 0, table_bytes = 0;
};
// False when no PE32+ image for ARM64 lies at base.
bool ReadImage(Memory &memory, U64 base, Image &image);
// The entry whose function holds pc; entry_address is where it lies in the
// program. False for a pc without an entry (a leaf function, or no code of
// this image).
bool Lookup(Memory &memory, const Image &image, U64 pc, RuntimeFunction &function, U64 &entry_address);

enum Result : unsigned
{
    Ok = 0,
    // A leaf whose return address is the pc itself: the walk cannot go on.
    BadFunctionTable,
    // The tables or the stack lie in memory the program cannot read.
    BadMemory,
    // An unwind code this unwinder does not know.
    BadCode,
};
// Takes the context one frame up: from the function that holds pc (function
// null: a leaf, the return address is in Lr) to its caller. handler and
// handler_data are the language handler of the function and what follows it
// in the record, zero without one or while pc stands in the prologue or an
// epilogue; frame is the establisher frame, the stack pointer of the caller.
Result VirtualUnwind(Memory &memory, U64 base, U64 pc, const RuntimeFunction *function, Context &context, U64 &handler,
                     U64 &handler_data, U64 &frame);
} // namespace Unwind
} // namespace Arm64
