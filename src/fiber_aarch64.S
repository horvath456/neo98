// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef __APPLE__
#define NAME(n) _##n
#else
#define NAME(n) n
#endif
.text
.p2align 2
.globl NAME(neo98_switch_context)
NAME(neo98_switch_context):
    stp x19, x20, [x0, #0]
    stp x21, x22, [x0, #16]
    stp x23, x24, [x0, #32]
    stp x25, x26, [x0, #48]
    stp x27, x28, [x0, #64]
    stp x29, x30, [x0, #80]
    mov x9, sp
    str x9, [x0, #96]
    stp d8, d9, [x0, #112]
    stp d10, d11, [x0, #128]
    stp d12, d13, [x0, #144]
    stp d14, d15, [x0, #160]
    mrs x9, fpcr
    mrs x10, fpsr
    stp x9, x10, [x0, #176]
    ldp x19, x20, [x1, #0]
    ldp x21, x22, [x1, #16]
    ldp x23, x24, [x1, #32]
    ldp x25, x26, [x1, #48]
    ldp x27, x28, [x1, #64]
    ldp x29, x30, [x1, #80]
    ldr x9, [x1, #96]
    mov sp, x9
    ldp d8, d9, [x1, #112]
    ldp d10, d11, [x1, #128]
    ldp d12, d13, [x1, #144]
    ldp d14, d15, [x1, #160]
    ldp x9, x10, [x1, #176]
    msr fpcr, x9
    msr fpsr, x10
    ret
.p2align 2
.globl NAME(neo98_fiber_entry)
NAME(neo98_fiber_entry):
    mov x0, x19
    bl NAME(neo98_fiber_run)
    brk #0
#ifndef __APPLE__
.section .note.GNU-stack,"",%progbits
#endif
