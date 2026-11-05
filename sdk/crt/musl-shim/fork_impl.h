/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* NEO98: musl's fork_impl.h names the locks a fork must take; there is no
   fork. ofl.c only publishes its lock there. Not part of musl. */
#ifndef NEO98_MUSL_FORK_IMPL_H
#define NEO98_MUSL_FORK_IMPL_H
extern volatile int *const __stdio_ofl_lockptr;
#endif
