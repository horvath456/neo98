// SPDX-License-Identifier: GPL-2.0-or-later
// Connect Win16 and Win32 thunk blocks, validate their signatures and checksums,
// and prepare target-table dispatch.
#pragma once

namespace Win98FlatThunk
{
// First dword of both thunk blocks. The original accepts no other value.
enum Signature : unsigned
{
    Flat = 0x3130534cu,  // "LS01", the FT_Thunk block
    Quick = 0x31304c53u, // "SL01", the QT_Thunk block
};
enum Reason : unsigned
{
    Detach = 0,
    Attach = 1,
};
enum Decision
{
    Reject,      // Original returns FALSE.
    ThreadNoop,  // Original returns TRUE without touching the blocks.
    Connect,
    Disconnect,
};

// Shared connector: dwReason zero disconnects, one connects,
// every other reason returns TRUE immediately. The signature test belongs to
// validation and rejects any other block.
inline Decision Classify(unsigned reason, unsigned signature)
{
    if (reason != Detach && reason != Attach)
        return ThreadNoop;
    if (reason == Detach)
        return Disconnect; // Unknown signatures also detach successfully.
    if (signature != Flat && signature != Quick)
        return Reject;
    return reason == Attach ? Connect : Disconnect;
}
// Validation precedes any link or code-template mutation.
inline bool Compatible(unsigned signature, unsigned checksum, unsigned other_signature, unsigned other_checksum)
{
    return (signature == Flat || signature == Quick) && signature == other_signature && checksum == other_checksum;
}

// stores the flat 16-bit target at +8 for "LS01" and at +0xc for
// "SL01". Both offsets are read back by the thunk code templates.
inline unsigned TargetOffset(unsigned signature)
{
    return signature == Flat ? 8u : 12u;
}

// The connector returns a non-zero value on both accepted paths and zero on a
// rejected block.
inline unsigned Result(Decision decision)
{
    return decision == Reject ? 0u : 1u;
}

// LS01 dispatch selects a Win16 target from the connector's target table. One
// entry reads the index from the caller frame; the other uses CL. Both enter
// QT_Thunk with the target in EDX.
enum Entry
{
    FrameIndex,     // index byte at [ebp-4].
    RegisterIndex,  // index byte in CL.
};
inline unsigned EntryOffsetField(Entry entry)
{
    return entry == FrameIndex ? 0x1cu : 0x20u;
}

// SHELL32 thunk callers marshal arguments as Win16 WORDs in Pascal order. The
// target removes those arguments; sign-extend its AX result before returning
// to Win32.
inline unsigned ArgumentBytes(unsigned words)
{
    return words * 2;
}
inline unsigned ReturnValue(unsigned result)
{
    return result & 0xffffu; // The caller's CWDE keeps AX only.
}
} // namespace Win98FlatThunk
