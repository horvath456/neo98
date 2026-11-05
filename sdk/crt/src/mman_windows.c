/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* Windows mappings for the byte-for-byte imported musl mallocng allocator.
   Allocation policy stays in musl; this adapter supplies reserve, commit and
   release with Windows page (4 KiB) and reservation (64 KiB) granularity.
   Mapping records live outside the module heap so DLL detach can free it. */
#include <windows.h>
#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
#include "lock.h"

/* The static CRT belongs to one PE module. Keep its Windows reservations
   outside musl's heap so DLL detach can release even retained small bins. */
struct Mapping { struct Mapping *next; void *base; };
static struct Mapping *mappings;
static volatile int mappings_lock;

void __neo98_release_mappings(void)
{
    __lock(&mappings_lock);
    struct Mapping *list = mappings;
    mappings = NULL;
    __unlock(&mappings_lock);
    while (list)
    {
        struct Mapping *next = list->next;
        VirtualFree(list->base, 0, MEM_RELEASE);
        HeapFree(GetProcessHeap(), 0, list);
        list = next;
    }
}

/* A new mapping: readable and writable memory that reads as zero. musl never
   asks for an address of its choice (it would only extend a program break,
   and there is none). */
void *__mmap(void *start, size_t length, int protection, int flags, int file, long long offset)
{
    void *memory;
    (void)offset;
    if (start || (flags & MAP_FIXED) || file != -1 || !length || (protection != (PROT_READ | PROT_WRITE) && protection != PROT_NONE))
    {
        errno = EINVAL;
        return MAP_FAILED;
    }
    memory = VirtualAlloc(NULL, length, MEM_RESERVE | (protection == PROT_NONE ? 0 : MEM_COMMIT),
                          protection == PROT_NONE ? PAGE_NOACCESS : PAGE_READWRITE);
    if (!memory) { errno = ENOMEM; return MAP_FAILED; }
    struct Mapping *mapping = HeapAlloc(GetProcessHeap(), 0, sizeof *mapping);
    if (!mapping)
    {
        VirtualFree(memory, 0, MEM_RELEASE);
        errno = ENOMEM;
        return MAP_FAILED;
    }
    mapping->base = memory;
    __lock(&mappings_lock);
    mapping->next = mappings;
    mappings = mapping;
    __unlock(&mappings_lock);
    return memory;
}
/* The mapping of a large block goes back to the system. musl gives the
   address and the length it got from __mmap, never a part: that is the one
   way Windows releases address space. */
int __munmap(void *start, size_t length)
{
    (void)length;
    __lock(&mappings_lock);
    struct Mapping **link = &mappings;
    while (*link && (*link)->base != start) link = &(*link)->next;
    if (*link && VirtualFree(start, 0, MEM_RELEASE))
    {
        struct Mapping *released = *link;
        *link = released->next;
        __unlock(&mappings_lock);
        HeapFree(GetProcessHeap(), 0, released);
        return 0;
    }
    __unlock(&mappings_lock);
    errno = EINVAL;
    return -1;
}
/* Windows cannot move or grow a mapping; realloc of a block that is one
   takes a new one and copies. */
void *__mremap(void *start, size_t old_length, size_t new_length, int flags, ...)
{
    (void)start; (void)old_length; (void)new_length; (void)flags;
    errno = ENOMEM;
    return MAP_FAILED;
}
/* MADV_DONTNEED on a free stretch in the middle of the heap: the system may
   have the pages, and the next access finds zeros. Under Windows the pages
   are given up (decommitted) and taken again at once, which costs nothing
   until they are touched. The stretch may span several reservations, when
   the heap grew by a mapping that happened to follow the one before; each
   is dealt with by itself. A page that cannot be taken again would be a
   hole in the heap, so a stretch is only given up as far as it is committed
   memory, and a failure to take it again ends the program like any other
   lost page (it does not happen: the address space is still reserved, and
   the memory was just given up). */
int __madvise(void *start, size_t length, int advice)
{
    uintptr_t at = (uintptr_t)start;
    const uintptr_t end = at + length;
    if (advice != MADV_DONTNEED) return 0;
    while (at < end)
    {
        MEMORY_BASIC_INFORMATION info;
        uintptr_t piece_end;
        if (VirtualQuery((void *)at, &info, sizeof info) != sizeof info || !info.RegionSize) break;
        piece_end = (uintptr_t)info.BaseAddress + info.RegionSize;
        if (piece_end > end) piece_end = end;
        if (info.State == MEM_COMMIT && info.Protect == PAGE_READWRITE && VirtualFree((void *)at, piece_end - at, MEM_DECOMMIT) &&
            !VirtualAlloc((void *)at, piece_end - at, MEM_COMMIT, PAGE_READWRITE))
            __builtin_trap();
        at = piece_end;
    }
    return 0;
}

/* mallocng commits one metadata page at a time, leaving its guard page
   reserved. These are actual 4 KiB pages inside a Windows reservation. */
int __mprotect(void *start, size_t length, int protection)
{
    if (protection != (PROT_READ | PROT_WRITE)) { errno = EINVAL; return -1; }
    if (VirtualAlloc(start, length, MEM_COMMIT, PAGE_READWRITE)) return 0;
    errno = ENOMEM;
    return -1;
}
