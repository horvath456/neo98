/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* NEO98: what the sources of musl expect from musl's own <features.h> and
   from its src/include/features.h. Not part of musl. */
#ifndef NEO98_MUSL_FEATURES_H
#define NEO98_MUSL_FEATURES_H
#include_next <features.h>
#define _GNU_SOURCE 1
#define _BSD_SOURCE 1
/* Everything is linked statically into one program: there is no visibility
   to restrict. */
#define hidden
#define weak __attribute__((__weak__))
/* A second name for a definition, which a definition of that name in another
   object replaces: a weak external of COFF. musl uses it for hooks (the
   flush at exit exists only in a program that uses streams). */
#define weak_alias(old, new) extern __typeof(old) new __attribute__((__weak__, __alias__(#old)))
#if __STDC_VERSION__ >= 201112L
#elif defined(__GNUC__)
#define _Noreturn __attribute__((__noreturn__))
#else
#define _Noreturn
#endif
#define __restrict restrict
#endif
