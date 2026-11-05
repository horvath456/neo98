/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
// Same x86 guest configuration as Unicorn 2.1.4, without a hosted OS.
#define TARGET_X86_64 1
#define TARGET_NAME "x86_64"
#define TARGET_I386 1
#define CONFIG_SOFTMMU 1
#define TARGET_SUPPORTS_MTTCG 1
