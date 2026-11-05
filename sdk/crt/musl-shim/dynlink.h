/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* NEO98: of musl's dynamic linker header the allocator needs the marks of a
   replaced malloc (always zero here: everything is linked statically), the
   question whether fresh memory is known to be zero, and the name under
   which the dynamic linker would hand it leftover memory. Not part of musl. */
#ifndef NEO98_MUSL_DYNLINK_H
#define NEO98_MUSL_DYNLINK_H
extern int __malloc_replaced;
extern int __aligned_alloc_replaced;
int __malloc_allzerop(void *);
void __malloc_donate(char *, char *);
#endif
