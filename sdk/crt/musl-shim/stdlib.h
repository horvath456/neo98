/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* NEO98: musl's src/include/stdlib.h. Not part of musl. */
#ifndef NEO98_MUSL_STDLIB_H
#define NEO98_MUSL_STDLIB_H
#include_next <stdlib.h>
typedef int (*__neo98_compare_r)(const void *, const void *, void *);
void __qsort_r(void *, size_t, size_t, __neo98_compare_r, void *);
/* qsort_r is no part of the runtime's own header: qsort.c defines it. */
void qsort_r(void *, size_t, size_t, __neo98_compare_r, void *);
/* The allocator by the names musl's sources call it. */
void *__libc_malloc_impl(size_t);
void *__libc_realloc(void *, size_t);
void __libc_free(void *);
size_t malloc_usable_size(void *);
#endif
