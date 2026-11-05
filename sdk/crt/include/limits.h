/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* The static C runtime of the NEO98 SDK (sdk/crt): the limits of the
   compiler, and those of the runtime beside them. */
#ifndef NEO98_CRT_LIMITS_H
#define NEO98_CRT_LIMITS_H
#include_next <limits.h>
/* The longest multibyte character: four bytes of UTF-8. */
#undef MB_LEN_MAX
#define MB_LEN_MAX 4
#ifndef SSIZE_MAX
#define SSIZE_MAX 0x7fffffffffffffffLL
#endif
#ifndef PATH_MAX
#define PATH_MAX 260
#endif
/* Numbered arguments of printf (%1$d). */
#ifndef NL_ARGMAX
#define NL_ARGMAX 9
#endif
#endif
