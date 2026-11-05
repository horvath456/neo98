/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* LONG __neo98_seh_call(void *first, void *frame, void *function, const BYTE *nonvolatile)

   Calls a filter or a termination handler of __try. Such a function is a
   piece of the function that holds the block: it addresses the locals of
   that function through x29 and may use its callee-saved registers. The
   system names them for the frame (x19..x29, then d8..d15); they are loaded
   here, and the frame pointer among them is the second argument, which is
   what the compiler's code takes as the frame. The unwind codes describe the
   prologue, so an exception inside the called function walks through here.
   sdk/start/seh.c; docs/ARM64-APPS.md. */
    .text
    .globl __neo98_seh_call
    .def __neo98_seh_call
    .scl 2
    .type 32
    .endef
    .p2align 2
    .seh_proc __neo98_seh_call
__neo98_seh_call:
    stp x29, x30, [sp, #-160]!
    .seh_save_fplr_x 160
    stp x19, x20, [sp, #16]
    .seh_save_regp x19, 16
    stp x21, x22, [sp, #32]
    .seh_save_regp x21, 32
    stp x23, x24, [sp, #48]
    .seh_save_regp x23, 48
    stp x25, x26, [sp, #64]
    .seh_save_regp x25, 64
    stp x27, x28, [sp, #80]
    .seh_save_regp x27, 80
    stp d8, d9, [sp, #96]
    .seh_save_fregp d8, 96
    stp d10, d11, [sp, #112]
    .seh_save_fregp d10, 112
    stp d12, d13, [sp, #128]
    .seh_save_fregp d12, 128
    stp d14, d15, [sp, #144]
    .seh_save_fregp d14, 144
    mov x29, sp
    .seh_set_fp
    .seh_endprologue
    ldp x19, x20, [x3, #0]
    ldp x21, x22, [x3, #16]
    ldp x23, x24, [x3, #32]
    ldp x25, x26, [x3, #48]
    ldp x27, x28, [x3, #64]
    ldp d8, d9, [x3, #88]
    ldp d10, d11, [x3, #104]
    ldp d12, d13, [x3, #120]
    ldp d14, d15, [x3, #136]
    ldr x1, [x3, #80]
    blr x2
    .seh_startepilogue
    ldp d14, d15, [sp, #144]
    .seh_save_fregp d14, 144
    ldp d12, d13, [sp, #128]
    .seh_save_fregp d12, 128
    ldp d10, d11, [sp, #112]
    .seh_save_fregp d10, 112
    ldp d8, d9, [sp, #96]
    .seh_save_fregp d8, 96
    ldp x27, x28, [sp, #80]
    .seh_save_regp x27, 80
    ldp x25, x26, [sp, #64]
    .seh_save_regp x25, 64
    ldp x23, x24, [sp, #48]
    .seh_save_regp x23, 48
    ldp x21, x22, [sp, #32]
    .seh_save_regp x21, 32
    ldp x19, x20, [sp, #16]
    .seh_save_regp x19, 16
    ldp x29, x30, [sp], #160
    .seh_save_fplr_x 160
    .seh_endepilogue
    ret
    .seh_endproc
