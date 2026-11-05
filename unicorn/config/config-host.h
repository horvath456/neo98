/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
// NEO98: freestanding AArch64 host, x86 soft MMU guest.
#define HOST_AARCH64 1
#define CONFIG_NEO98_CIRCLE 1
#define CONFIG_TCG 1
#define CONFIG_INT128 1
#define CONFIG_CMPXCHG128 1
#define CONFIG_STATIC_ASSERT 1
#define CONFIG_PRAGMA_DIAGNOSTIC_AVAILABLE 1
#define QEMU_VERSION "5.0.1"
#define QEMU_VERSION_MAJOR 5
#define QEMU_VERSION_MINOR 0
#define QEMU_VERSION_MICRO 1
#define HOST_DSOSUF ".a"
