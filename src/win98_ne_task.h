// SPDX-License-Identifier: GPL-2.0-or-later
// KRNL386 contracts used by the Win16 loader: global handles, GlobalAlloc size
// rounding, automatic data segments and the task database fields programs read.
#pragma once
namespace Win98NeTask
{
// the size arithmetic GlobalAlloc and GlobalReAlloc share.
// The request is rounded up to sixteen bytes; a request above 0x00ff0000, and
// one whose rounding carries, returns SizeFailure for the allocator to reject.
constexpr unsigned SizeLimit = 0x00ff0000, SizeFailure = 0x7fffffff;
inline unsigned RoundSize(unsigned bytes)
{
    const unsigned raised = bytes + 15;
    if (raised < bytes)
        return SizeFailure;
    const unsigned rounded = raised & ~15u;
    return rounded > SizeLimit ? SizeFailure : rounded;
}
// KRNL386 reports E002 for these bits. E002 has bit 8000h set, so
// Windows 98 returns to the allocator instead of rejecting the request.
constexpr unsigned WarnedAllocationFlags = 0x8c8d;
// The flag bits the allocator and the block table read, as GMEM_ spells them.
constexpr unsigned Modify = 0x0080; // KRNL386, distinct from GMEM_ZEROINIT.
constexpr unsigned Moveable = 0x0002, ZeroInit = 0x0040, Discardable = 0x0100, DdeShare = 0x2000;
// GlobalFlags answers the lock count in the low byte and the flag byte of the
// block in the high byte. The lookup puts
// bit 40h of that byte there when the descriptor of the block is not present,
// so a discarded block reports 4000h, which is what GMEM_DISCARDED spells.
constexpr unsigned Discarded = 0x4000;
// Windows 98 derives the number of 64 KiB pieces from the limit of the
// first descriptor. A block with no memory keeps one descriptor.
inline unsigned TilesOf(unsigned size)
{
    return size ? ((size - 1) >> 16) + 1 : 1;
}
// A moveable block answers the selector minus one, and the
// zero-length block of GlobalAlloc(GMEM_MOVEABLE, 0) does the same. A fixed
// block answers its selector unchanged., the core of
// GlobalLock, turns a handle back into a selector with "or dl, 1", which is
// exact for both shapes.
inline unsigned HandleOf(unsigned selector, bool moveable)
{
    return moveable ? selector - 1 : selector;
}
inline unsigned SelectorOf(unsigned handle)
{
    return handle ? (handle | 1) : 0;
}
// in GlobalFree, in its core and in GlobalReAlloc: a
// handle that is a multiple of eight is decremented first. Our selectors
// always carry the table and privilege bits, so this only catches a caller
// that hands in a selector it built itself.
inline unsigned NormalizeHandle(unsigned handle)
{
    return handle && !(handle & 7) ? handle - 1 : handle;
}

// The instance data at offset zero of the automatic data segment. InitTask
// fills the three stack words, and the ported local heap
// writes the heap word at offset 6 (win98_local_heap.h, e.W(6, heap)).
constexpr unsigned InstanceHeap = 0x06, InstanceAtoms = 0x08, InstanceStackTop = 0x0a,
                   InstanceStackMin = 0x0c, InstanceStackBottom = 0x0e;
// the margin InitTask leaves above the lowest usable stack word.
constexpr unsigned InstanceStackMargin = 0x96;

// The task data. GetExePtr walks the chain and reads the
// instance and module fields; GetTaskQueue reads the queue, and
// IsTask compares the signature. The remaining words are
// left at zero rather than guessed.
constexpr unsigned TaskNext = 0x00, TaskPdb = 0x02, TaskStackSegment = 0x04, TaskStackPointer = 0x06,
                   TaskEvents = 0x08, TaskInstance = 0x1c, TaskModule = 0x1e, TaskQueue = 0x20,
                   TaskSignature = 0xfa;
constexpr unsigned TaskSignatureValue = 0x4454; // "TD"
constexpr unsigned TaskBytes = 0x210;

// A Win16 PSP stores the environment selector at offset 2Ch. Startup code
// loads that selector before reading the environment strings.
constexpr unsigned ProgramPrefixBytes = 0x100, ProgramPrefixCommand = 0x80,
                   ProgramPrefixEnvironment = 0x2c;
} // namespace Win98NeTask
