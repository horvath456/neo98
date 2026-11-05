/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* The static C runtime of the NEO98 SDK: how a thread sleeps on a lock of
   musl that another thread holds, and how it is woken. musl's locks (the
   ones of the allocator, and __lock.c for the list of open streams) take
   and give a free lock with one atomic operation and come here only when
   two threads meet; under Linux this is the futex system call. Windows has
   had nothing of the kind before Windows 8, so here it is a list of the
   sleeping threads, each with an event of its own, which KERNEL32 has had
   since Windows 95. Own code. docs/ARM64-APPS.md, "The allocator". */
#include <windows.h>
#include <stdarg.h>
#include "pthread_impl.h"

struct Waiter
{
    struct Waiter *next;
    volatile int *address;
    HANDLE event;
};
/* The list is held for a few instructions at a time. */
static volatile int guard;
static struct Waiter *first;

static void Guard(void)
{
    unsigned turns = 0;
    while (a_swap(&guard, 1))
    {
        if (++turns < 64) a_spin();
        else Sleep(0);
    }
}
static void Unguard(void) { a_store(&guard, 0); }

/* Sleeps as long as the int at the address has the value. The caller checks
   again after every return, so returning early is allowed (and is what
   happens when the system has no event left). */
void __futexwait(volatile void *address, int value, int private_)
{
    struct Waiter self = {NULL, (volatile int *)address, CreateEventA(NULL, FALSE, FALSE, NULL)};
    struct Waiter **link;
    (void)private_;
    if (!self.event) { Sleep(1); return; }
    Guard();
    /* Read under the guard: whoever changes the value and then wakes takes
       the guard either after this thread stands in the list, or changed the
       value before this read. */
    if (*self.address != value)
    {
        Unguard();
        CloseHandle(self.event);
        return;
    }
    for (link = &first; *link; link = &(*link)->next) {}
    *link = &self;
    Unguard();
    WaitForSingleObject(self.event, INFINITE);
    CloseHandle(self.event);
}

/* Wakes up to `count` threads that sleep on the address, the one that came
   first before the others. */
void __wake(volatile void *address, int count, int private_)
{
    (void)private_;
    while (count-- > 0)
    {
        struct Waiter **link;
        HANDLE event = NULL;
        Guard();
        for (link = &first; *link; link = &(*link)->next)
            if ((*link)->address == (volatile int *)address)
            {
                /* The record lies on the stack of the sleeping thread: nothing
                   of it is read once that thread may run. */
                event = (*link)->event;
                *link = (*link)->next;
                break;
            }
        Unguard();
        if (!event) return;
        SetEvent(event);
    }
}

/* The two system calls of Linux the imported sources make (musl-shim/
   syscall.h). The program break: there is none. The futex, of which only
   waiting comes this way (__wait.c; waking is __wake above). */
long __neo98_syscall(long number, ...)
{
    if (number == SYS_futex)
    {
        va_list arguments;
        volatile void *address;
        int value;
        va_start(arguments, number);
        address = va_arg(arguments, volatile void *);
        (void)va_arg(arguments, int);
        value = va_arg(arguments, int);
        va_end(arguments);
        __futexwait(address, value, 1);
    }
    return 0;
}
