/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* Windows has no ELF auxiliary vector. musl uses its own fallback when
   the empty adapter vector has no AT_RANDOM entry. */
#ifndef NEO98_MUSL_ELF_H
#define NEO98_MUSL_ELF_H
#define AT_RANDOM 25
#endif
