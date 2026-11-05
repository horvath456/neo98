/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* The static C runtime of the NEO98 SDK (sdk/crt). */
#ifndef NEO98_CRT_SYS_TYPES_H
#define NEO98_CRT_SYS_TYPES_H
#include <stddef.h>
typedef long long ssize_t;
#ifndef NEO98_CRT_OFF_T
#define NEO98_CRT_OFF_T
typedef long long off_t;
#endif
#endif
