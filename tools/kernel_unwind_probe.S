/* SPDX-License-Identifier: GPL-2.0-or-later */
.text
.globl _unwind_call
.globl _unwind_return
_unwind_call:
 pushl %ebp
 movl %esp,%ebp
 pushl %ebx
 pushl %esi
 pushl %edi
 movl %esp,_unwind_expected_sp
 pushl 16(%ebp)
 pushl 12(%ebp)
 pushl $_unwind_forbidden
 pushl 8(%ebp)
 calll *__imp__RtlUnwind@16
_unwind_return:
 cmpl _unwind_expected_sp,%esp
 jne _unwind_forbidden
 popl %edi
 popl %esi
 popl %ebx
 leave
 retl
_unwind_forbidden:
 pushl $22899
 calll *__imp__ExitProcess@4
