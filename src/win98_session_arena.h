// SPDX-License-Identifier: GPL-2.0-or-later
// The address plan of the session-wide arena and the record of one session
// block.
// Win98 keeps one area that every process sees at the same linear address:
// the sections with IMAGE_SCN_MEM_SHARED of the loaded images, the heaps from
// HeapCreate with HEAP_SHARED, and the views of named file mappings. This
// header holds only the plan and the plain data. Everything that needs a
// process, a Unicorn address space or page metadata lives in
// src/x86_session_arena.cpp.
#pragma once
namespace Win98SessionArena
{
using U = unsigned;
// HeapCreate turns the HEAP_SHARED flag into the VMM arena tag
// 80060000h, and _PageReserve searches that arena above 80000000h. The session hands out the lower quarter of that window, so the
// upper part stays with the per-process arena for unnamed mappings.
constexpr U Base = 0x80000000u;
constexpr U Limit = 0x90000000u;
// The VMM allocation granularity every reservation of the shared arena keeps.
constexpr U Granularity = 0x10000u;
constexpr U PageSize = 0x1000u;
// A shared section is small. The largest one measured in this tree is the
// resource section of MPRSERV with 9E28h bytes, and the data sections are
// between 38h and 3400h bytes. Heaps are the large blocks: OLE32 asks for one
// MiB, MPR for 200000h.
constexpr U BlockLimit = 0x4000000u;
constexpr unsigned BlockCapacity = 96;
constexpr unsigned HolderCapacity = 20;
constexpr unsigned NameBytes = 80;
enum Kind : unsigned char
{
    KindFree = 0,
    KindSection = 1,   // image base + RVA, address given by the image
    KindHeap = 2,      // HeapCreate with HEAP_SHARED, address given by the session
    KindView = 3,      // view of a named file mapping, address given by the session
    // Memory of a native VxD at its ring-0 address above C0000000h. Win98 maps
    // the system arena into every context, and a ring-3 DLL may read what a
    // VxD hands it there: MSAFD.DLL reads the address family out of the
    // WSOCK2 socket object. Present in every
    // process of the session, read-only for the guest, written by the host.
    KindSystem = 4,
    KindVirtual = 5   // VirtualAlloc with MEM_SHARED, released by VirtualFree
};
// Where the native VxDs keep objects a ring-3 caller dereferences. Win98's
// VMM heap lies in the same region; the address itself is only a name.
constexpr U SystemBase = 0xc1100000u;
// The address of a session block never moves while the session lives. A block
// of kind KindSection sits where its image sits, the other two kinds get their
// address from Reserve below.
struct Plan
{
    U next = Base;
    // Bump allocation only. A released block keeps its linear range for the
    // rest of the session, which is what the shared heap of MPR needs after
    // HeapDestroy.
    U Reserve(U bytes)
    {
        if (!bytes || bytes > BlockLimit)
            return 0;
        const U size = (bytes + (Granularity - 1)) & ~(Granularity - 1);
        if (size > Limit - next)
            return 0;
        const U base = next;
        next += size;
        return base;
    }
};
inline U RoundPages(U bytes) { return (bytes + (PageSize - 1)) & ~(PageSize - 1); }
inline bool InSessionWindow(U address) { return address >= Base && address < Limit; }
// The shared arena of Win98. Every context has it in
// its page tables, so a section that lies there is present in a process that
// never loaded its module: DDHELP.EXE reads a string out of the shared `.data`
// of DSOUND without having DSOUND. Below this line the address
// belongs to the private arena, and a section there exists only where its
// module is loaded.
constexpr U SharedArena = 0x80000000u;
constexpr U SharedArenaEnd = 0xc0000000u;
inline bool InSharedArena(U address)
{
    return address >= SharedArena && address < SharedArenaEnd;
}
} // namespace Win98SessionArena
