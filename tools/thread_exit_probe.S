/* SPDX-License-Identifier: GPL-2.0-or-later
 * setjmp and longjmp for tools/thread_exit_fixture.c, around an SEH frame.
 * Frame: +0 next, +4 handler (set by the caller), +8 resume, +12 esp. */
.text
.globl _guarded
.globl _leap
/* U guarded(struct Frame *frame, void (*body)(U), U argument) */
_guarded:
 pushl %ebp
 movl %esp,%ebp
 pushl %ebx
 pushl %esi
 pushl %edi
 movl 8(%ebp),%eax
 movl %esp,12(%eax)
 movl $1f,8(%eax)
 movl %fs:0,%ecx
 movl %ecx,(%eax)
 movl %eax,%fs:0
 pushl 16(%ebp)
 calll *12(%ebp)
 addl $4,%esp
 movl 8(%ebp),%eax
 movl (%eax),%ecx
 movl %ecx,%fs:0
 xorl %eax,%eax
 jmp 2f
1:
 movl $1,%eax
2:
 popl %edi
 popl %esi
 popl %ebx
 popl %ebp
 retl
/* void leap(struct Frame *frame): back into guarded behind its body. */
_leap:
 movl 4(%esp),%eax
 movl 12(%eax),%esp
 jmpl *8(%eax)
