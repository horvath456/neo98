/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* sdk/start: the stack probe the compiler calls. docs/ARM64-APPS.md. */
/* __chkstk of the Windows ARM64 ABI: x15 holds the size of the new frame in
   units of 16 bytes. Touch every page from the stack pointer down so the guard
   page moves; x15 stays as it is, x16 and x17 are scratch. */
__asm__(".globl __chkstk\n"
        "__chkstk:\n"
        "    lsl x16, x15, #4\n"
        "    mov x17, sp\n"
        "1:  sub x17, x17, #4096\n"
        "    subs x16, x16, #4096\n"
        "    ldr xzr, [x17]\n"
        "    b.gt 1b\n"
        "    ret\n");
