/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* The static C runtime of the NEO98 SDK (sdk/crt): what the Windows runtime
   and the C libraries of Unix declare here beside <stdlib.h>. */
#ifndef NEO98_CRT_MALLOC_H
#define NEO98_CRT_MALLOC_H
#include <stdlib.h>
#ifdef __cplusplus
extern "C" {
#endif
/* The bytes a block has room for, at least what it was asked with (the
   Windows runtime's name and musl's). */
size_t _msize(void *);
size_t malloc_usable_size(void *);
/* Room in the frame of the calling function, gone when it returns. The
   compiler does it; a frame of more than a page is probed (__chkstk). */
#ifndef alloca
#define alloca(size) __builtin_alloca(size)
#endif
#ifndef _alloca
#define _alloca(size) __builtin_alloca(size)
#endif
#ifdef __cplusplus
}
#endif
#endif
