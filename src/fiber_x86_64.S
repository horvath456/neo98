// SPDX-License-Identifier: GPL-2.0-or-later
// SysV x86_64 cooperative contexts. Slots 10, 11 and 12 retain the frame,
// continuation and stack conventions used by Fiber::SavedFrame.
.text
.p2align 4
.globl neo98_switch_context
.type neo98_switch_context,@function
neo98_switch_context:
    movq %rbx, 0(%rdi)
    movq %r12, 8(%rdi)
    movq %r13, 16(%rdi)
    movq %r14, 24(%rdi)
    movq %r15, 32(%rdi)
    movq %rbp, 80(%rdi)
    movq (%rsp), %rax
    movq %rax, 88(%rdi)
    leaq 8(%rsp), %rax
    movq %rax, 96(%rdi)
    stmxcsr 176(%rdi)
    fnstcw 184(%rdi)
    movq 0(%rsi), %rbx
    movq 8(%rsi), %r12
    movq 16(%rsi), %r13
    movq 24(%rsi), %r14
    movq 32(%rsi), %r15
    movq 80(%rsi), %rbp
    movq 96(%rsi), %rsp
    ldmxcsr 176(%rsi)
    fldcw 184(%rsi)
    jmp *88(%rsi)
.size neo98_switch_context,.-neo98_switch_context
.p2align 4
.globl neo98_fiber_entry
.type neo98_fiber_entry,@function
neo98_fiber_entry:
    subq $8, %rsp
    movq %rbx, %rdi
    call neo98_fiber_run
    ud2
.size neo98_fiber_entry,.-neo98_fiber_entry
.section .note.GNU-stack,"",@progbits
