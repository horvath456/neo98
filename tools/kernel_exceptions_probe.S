/* SPDX-License-Identifier: GPL-2.0-or-later */
.text
.globl _seh_register_probe
.globl _seh_resume_return
.globl _seh_resume_target
.globl _seh_leap_round
.globl _seh_leap_resume
.globl _seh_leap_longjmp
_seh_register_probe:
 pushl %ebp
 movl %esp,%ebp
 pushl %ebx
 pushl %esi
 pushl %edi
 movl %esp,_seh_expected_sp
 pushl $0
 pushl $0
 pushl $0
 pushl $0xe1230003
 movl $0x11111111,%eax
 movl $0x22222222,%ebx
 calll *__imp__RaiseException@16
_seh_resume_return:
 movl $22720,%eax
 jmp 2f
_seh_resume_target:
 cmpl $0xabcdef01,%eax
 jne 1f
 cmpl $0x23456789,%ebx
 jne 1f
 cmpl _seh_expected_sp,%esp
 jne 1f
 xorl %eax,%eax
 jmp 2f
1:
 movl $22721,%eax
2:
 popl %edi
 popl %esi
 popl %ebx
 leave
 retl

/* A round of the handler that does not return. The round saves the frame the
   way setjmp does, faults, and is continued by _seh_leap_longjmp out of the
   handler. Reaching the line behind the fault means the handler returned, and
   the round answers the failure code then. */
_seh_leap_round:
 pushl %ebp
 movl %esp,%ebp
 pushl %ebx
 pushl %esi
 pushl %edi
 movl %esp,_seh_leap_state
 movl %ebp,_seh_leap_state+4
 xorl %eax,%eax
 movl (%eax),%eax
 movl $22750,%eax
 jmp 4f
_seh_leap_resume:
 xorl %eax,%eax
4:
 popl %edi
 popl %esi
 popl %ebx
 leave
 retl
/* The non-local jump out of the handler: the stack of the round comes back and
   the round continues behind its fault. The three saved registers are on that
   stack and the epilogue of the round pops them. */
_seh_leap_longjmp:
 movl _seh_leap_state+4,%ebp
 movl _seh_leap_state,%esp
 jmp _seh_leap_resume
