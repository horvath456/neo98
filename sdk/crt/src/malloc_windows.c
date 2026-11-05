/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* The static C runtime of the NEO98 SDK: the names of <stdlib.h> and
   <malloc.h> for memory. The allocator behind them is musl's mallocng,
   which works inside the program: no call of the system for a block, only
   for a large piece of the heap (mman_windows.c). calloc is musl's own file.
   Own code: the names and what the Windows runtime promises beyond the C
   standard. docs/ARM64-APPS.md, "The allocator". */
#include <errno.h>
#include <malloc.h>
#include <stdlib.h>

void *malloc(size_t size) { return __libc_malloc_impl(size); }
void free(void *memory) { __libc_free(memory); }
/* realloc(p, 0) frees and answers NULL, as the Windows runtime does. */
void *realloc(void *memory, size_t size)
{
    if (memory && !size) { __libc_free(memory); return NULL; }
    return __libc_realloc(memory, size);
}
/* The bytes the block has room for: at least what it was asked with. */
size_t _msize(void *memory) { return malloc_usable_size(memory); }
/* The Windows names of aligned memory. musl's aligned block is a block like
   any other: free and realloc take it. */
void *_aligned_malloc(size_t size, size_t alignment)
{
    if (!alignment || (alignment & (alignment - 1))) { errno = EINVAL; return NULL; }
    return aligned_alloc(alignment, size);
}
void _aligned_free(void *memory) { __libc_free(memory); }
