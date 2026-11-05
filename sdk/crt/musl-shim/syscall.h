/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* NEO98: musl's system call header. The stream layer reaches the system
   through sdk/crt/src/stdio_windows.c and makes no system call. The
   imported sources make two calls of Linux: the allocator asks for the
   program break before it maps memory (there is none under Windows; the
   answer 0 is never the address it asked for, so it maps), and a lock that
   another thread holds waits (futex). sdk/crt/src/futex_windows.c answers
   both. Not part of musl. */
#ifndef NEO98_MUSL_SYSCALL_H
#define NEO98_MUSL_SYSCALL_H
#define SYS_brk 1
#define SYS_futex 2
long __neo98_syscall(long number, ...);
#define __syscall(...) __neo98_syscall(__VA_ARGS__)
#endif
