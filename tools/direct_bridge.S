// SPDX-License-Identifier: GPL-2.0-or-later
#include "direct_bridge.h"
.text
.p2align 4
.global neo98_direct_enter
.type neo98_direct_enter,@function
neo98_direct_enter:
    movq %rsp,DIRECT_HOST_SP(%rdi)
    movq %rbx,DIRECT_HOST_RBX(%rdi)
    movq %rbp,DIRECT_HOST_RBP(%rdi)
    movq %r12,DIRECT_HOST_R12(%rdi)
    movq %r13,DIRECT_HOST_R13(%rdi)
    movq %r14,DIRECT_HOST_R14(%rdi)
    movq %r15,DIRECT_HOST_R15(%rdi)
    rdfsbase %rax
    movq %rax,DIRECT_HOST_FS(%rdi)
    rdgsbase %rax
    movq %rax,DIRECT_HOST_GS(%rdi)
    movw %ds,DIRECT_HOST_DS(%rdi)
    movw %es,DIRECT_HOST_ES(%rdi)
    movw %fs,DIRECT_HOST_FSS(%rdi)
    movw %gs,DIRECT_HOST_GSS(%rdi)
    movw %ss,DIRECT_HOST_SS(%rdi)
    pushfq
    popq DIRECT_HOST_FLAGS(%rdi)
    fxsave64 DIRECT_HOST_FP(%rdi)
    movq %rdi,%r12
    movq %rdi,neo98_direct_active_frame(%rip)
    pushq 96(%r12)
    pushq 32(%r12)
    pushq 72(%r12)
    pushq 88(%r12)
    pushq 64(%r12)
    movw 80(%r12),%ax
    movw %ax,%es
    movw 104(%r12),%ax
    movw %ax,%ds
    movw 112(%r12),%ax
    movw %ax,%fs
    movw 120(%r12),%ax
    movw %ax,%gs
    fxrstor64 DIRECT_GUEST_FP(%r12)
    movl 0(%r12),%eax
    movl 8(%r12),%ecx
    movl 16(%r12),%edx
    movl 24(%r12),%ebx
    movl 40(%r12),%ebp
    movl 48(%r12),%esi
    movl 56(%r12),%edi
    iretq
.global neo98_direct_enter_end
neo98_direct_enter_end:
.size neo98_direct_enter,.-neo98_direct_enter

// This position-independent fragment is copied below 4 GiB. R12 survives
// compatibility-mode execution and points at this Run's host frame.
.p2align 4
.global neo98_direct_gate_start
neo98_direct_gate_start:
    movl %eax,0(%r12)
    movl %ecx,8(%r12)
    movl %edx,16(%r12)
    movl %ebx,24(%r12)
    movl %esp,32(%r12)
    movl %ebp,40(%r12)
    movl %esi,48(%r12)
    movl %edi,56(%r12)
    movq DIRECT_HOST_SP(%r12),%rsp
    pushfq
    popq 72(%r12)
    movw %es,80(%r12)
    movw %ss,96(%r12)
    movw %ds,104(%r12)
    movw %fs,112(%r12)
    movw %gs,120(%r12)
    fxsave64 DIRECT_GUEST_FP(%r12)
    movq $1,DIRECT_EVENT(%r12)
    // The absolute address is patched in the copied fragment.
    .byte 0x48,0xb8
    .quad 0
    jmp *%rax
.global neo98_direct_gate_end
neo98_direct_gate_end:

.global neo98_direct_finish
.type neo98_direct_finish,@function
neo98_direct_finish:
    pushq DIRECT_HOST_FLAGS(%r12)
    popfq
    movw DIRECT_HOST_DS(%r12),%ax
    movw %ax,%ds
    movw DIRECT_HOST_ES(%r12),%ax
    movw %ax,%es
    movw DIRECT_HOST_FSS(%r12),%ax
    movw %ax,%fs
    movw DIRECT_HOST_GSS(%r12),%ax
    movw %ax,%gs
    movw DIRECT_HOST_SS(%r12),%ax
    movw %ax,%ss
    movq DIRECT_HOST_FS(%r12),%rax
    wrfsbase %rax
    movq DIRECT_HOST_GS(%r12),%rax
    wrgsbase %rax
    fxrstor64 DIRECT_HOST_FP(%r12)
    movq DIRECT_HOST_SP(%r12),%rsp
    movq DIRECT_HOST_RBX(%r12),%rbx
    movq DIRECT_HOST_RBP(%r12),%rbp
    movq DIRECT_HOST_R13(%r12),%r13
    movq DIRECT_HOST_R14(%r12),%r14
    movq DIRECT_HOST_R15(%r12),%r15
    movq DIRECT_HOST_R12(%r12),%r12
    movq $0,neo98_direct_active_frame(%rip)
    ret
.size neo98_direct_finish,.-neo98_direct_finish

// A kernel signal starts in 64-bit mode with the guest's FS/GS bases.
// Restore host TLS before entering C, including when a transition was
// interrupted. Signals on other host threads keep those threads' TLS.
.p2align 4
.global neo98_direct_signal
.type neo98_direct_signal,@function
.weak neo98_direct_handle_signal
neo98_direct_signal:
    subq $56,%rsp
    rdfsbase %rax
    movq %rax,0(%rsp)
    rdgsbase %rax
    movq %rax,8(%rsp)
    movw %ds,16(%rsp)
    movw %es,18(%rsp)
    movw %fs,20(%rsp)
    movw %gs,22(%rsp)
    movw %ss,24(%rsp)
    movq neo98_direct_active_frame(%rip),%r8
    testq %r8,%r8
    jz .Lsignal_no_frame
    movl $186,%eax
    syscall
    cmpq neo98_direct_active_tid(%rip),%rax
    jne .Lsignal_no_frame
    cmpw $0x33,184(%rdx)
    je .Lsignal_host_segments
    movw 16(%rsp),%ax
    movw %ax,104(%r8)
    movw 18(%rsp),%ax
    movw %ax,80(%r8)
    movw 20(%rsp),%ax
    movw %ax,112(%r8)
    movw 22(%rsp),%ax
    movw %ax,120(%r8)
.Lsignal_host_segments:
    pushq DIRECT_HOST_FLAGS(%r8)
    popfq
    movw DIRECT_HOST_DS(%r8),%ax
    movw %ax,%ds
    movw DIRECT_HOST_ES(%r8),%ax
    movw %ax,%es
    movw DIRECT_HOST_FSS(%r8),%ax
    movw %ax,%fs
    movw DIRECT_HOST_GSS(%r8),%ax
    movw %ax,%gs
    movw DIRECT_HOST_SS(%r8),%ax
    movw %ax,%ss
    movq DIRECT_HOST_FS(%r8),%rax
    wrfsbase %rax
    movq DIRECT_HOST_GS(%r8),%rax
    wrgsbase %rax
    fxrstor64 DIRECT_HOST_FP(%r8)
    movq %r8,%rcx
    jmp .Lsignal_c
.Lsignal_no_frame:
    xorq %rcx,%rcx
.Lsignal_c:
    cld
    call neo98_direct_handle_signal@PLT
    testl %eax,%eax
    jnz .Lsignal_done
    movw 16(%rsp),%ax
    movw %ax,%ds
    movw 18(%rsp),%ax
    movw %ax,%es
    movw 20(%rsp),%ax
    movw %ax,%fs
    movw 22(%rsp),%ax
    movw %ax,%gs
    movw 24(%rsp),%ax
    movw %ax,%ss
    movq 0(%rsp),%rax
    wrfsbase %rax
    movq 8(%rsp),%rax
    wrgsbase %rax
.Lsignal_done:
    addq $56,%rsp
    ret
.size neo98_direct_signal,.-neo98_direct_signal
.bss
.p2align 3
.global neo98_direct_active_frame
neo98_direct_active_frame:
    .quad 0
.global neo98_direct_active_tid
neo98_direct_active_tid:
    .quad 0
.section .note.GNU-stack,"",@progbits
