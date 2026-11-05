/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* NEO98: musl's <sys/mman.h> as far as its allocator uses it, by the
   internal names musl calls. The functions take and return address space
   through VirtualAlloc and VirtualFree (sdk/crt/src/mman_windows.c). Not
   part of musl. */
#ifndef NEO98_MUSL_SYS_MMAN_H
#define NEO98_MUSL_SYS_MMAN_H
#include <stddef.h>
#define MAP_FAILED ((void *)-1)
#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#define MAP_ANON 0x20
#define MAP_ANONYMOUS MAP_ANON
#define MREMAP_MAYMOVE 1
#define MADV_DONTNEED 4
#define MADV_FREE 8 /* parsed by mallocng; USE_MADV_FREE remains zero */
void *__mmap(void *, size_t, int, int, int, long long);
int __munmap(void *, size_t);
int __mprotect(void *, size_t, int);
#define munmap __munmap
#define mprotect __mprotect
void *__mremap(void *, size_t, size_t, int, ...);
int __madvise(void *, size_t, int);
#endif
