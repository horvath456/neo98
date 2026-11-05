/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* NEO98: what the imported sources of musl ask of its thread record
   (src/internal/pthread_impl.h): the locale in force and, for the lock of a
   stream, a Windows thread number. The locale belongs to the process;
   the temporary record supplies the current thread ID to musl without
   owning another TLS slot. sdk/crt/src/stdlib.c supplies the Windows call.

   musl's internal locks (the ones of the allocator, which wait in
   src/thread/__wait.c, and src/thread/__lock.c for the list of open streams)
   sleep and wake through the futex of Linux. Here that is a pair of
   functions over events of KERNEL32 (sdk/crt/src/futex_windows.c); a lock
   nobody else wants never reaches them. Not part of musl. */
#ifndef NEO98_MUSL_PTHREAD_IMPL_H
#define NEO98_MUSL_PTHREAD_IMPL_H
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include "atomic.h"
#include "libc.h"
#include "syscall.h"
/* musl's src/internal/futex.h, as far as __wait.c names it. */
#define FUTEX_WAIT 0
#define FUTEX_PRIVATE 128
struct __pthread
{
    int tid;
    struct __locale_struct *locale;
};
/* The identity stays constant during execution of one Windows thread.
   Locale-only uses of the temporary record must not make a system call. */
unsigned long __neo98_stdio_thread_id(void) __attribute__((const));
#define __pthread_self() (&(struct __pthread){(int)__neo98_stdio_thread_id(), &__libc.global_locale})
/* musl's src/thread/__wait.c: turns a few times, then sleeps. */
void __wait(volatile int *address, volatile int *waiters, int value, int private_);
/* Sleeps while the int at the address holds the value; may return early. */
void __futexwait(volatile void *address, int value, int private_);
/* Wakes up to count threads that sleep on the address. */
void __wake(volatile void *address, int count, int private_);
#endif
