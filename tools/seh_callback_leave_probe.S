/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A round that raises a software exception and is continued by
   _seh_raise_longjmp out of the handler, as a __except block continues after
   RtlUnwind. Reaching the line behind RaiseException means the
   handler returned, and the round answers the failure code then. */
.text
.globl _seh_raise_round
.globl _seh_raise_resume
.globl _seh_raise_longjmp
_seh_raise_round:
 pushl %ebp
 movl %esp,%ebp
 pushl %ebx
 pushl %esi
 pushl %edi
 movl %esp,_seh_raise_state
 movl %ebp,_seh_raise_state+4
 pushl $0
 pushl $0
 pushl $0
 pushl $0xe1230010
 calll *__imp__RaiseException@16
 movl $22850,%eax
 jmp 6f
_seh_raise_resume:
 xorl %eax,%eax
6:
 popl %edi
 popl %esi
 popl %ebx
 leave
 retl
_seh_raise_longjmp:
 movl _seh_raise_state+4,%ebp
 movl _seh_raise_state,%esp
 jmp _seh_raise_resume
