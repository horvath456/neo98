/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* NEO98: musl's src/internal/lock.h. The lock itself is musl's
   (src/thread/__lock.c, here for the list of open streams): one
   compare-and-swap when nobody else wants it. Not part of musl. */
#ifndef NEO98_MUSL_LOCK_H
#define NEO98_MUSL_LOCK_H
void __lock(volatile int *);
void __unlock(volatile int *);
#define LOCK(x) __lock(x)
#define UNLOCK(x) __unlock(x)
#endif
