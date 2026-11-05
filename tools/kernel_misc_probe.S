/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SEH frames around DebugBreak and FatalExit for tools/kernel_misc_fixture.c. */
.text
.globl _probe_breakpoint
_probe_breakpoint:
 pushl %ebp
 movl %esp,%ebp
 pushl %ebx
 pushl %esi
 pushl %edi
 pushl 8(%ebp)
 pushl %fs:0
 movl %esp,%fs:0
 movl $0x11223344,%ebx
 movl $0x55667788,%esi
 movl $0x99aabbcc,%edi
 calll *__imp__DebugBreak@0
 movl $1,%eax
 cmpl $0x11223344,%ebx
 jne 1f
 cmpl $0x55667788,%esi
 jne 1f
 cmpl $0x99aabbcc,%edi
 jne 1f
 movl $0x42,%eax
1:
 popl %fs:0
 addl $4,%esp
 popl %edi
 popl %esi
 popl %ebx
 leave
 retl
.globl _probe_fatalexit
_probe_fatalexit:
 pushl %ebp
 movl %esp,%ebp
 pushl 8(%ebp)
 pushl %fs:0
 movl %esp,%fs:0
 pushl $3
 calll *__imp__FatalExit@4
 popl %fs:0
 addl $4,%esp
 leave
 retl
