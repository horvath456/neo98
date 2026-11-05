/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* The static C runtime of the NEO98 SDK: <signal.h>, and the filter of the
   process the start code installs. The Windows runtime has no signals of the
   system; raise calls the handler of the program, and an exception nobody
   handled reaches the handler of SIGSEGV, SIGILL or SIGFPE on its way out.
   Own code. docs/ARM64-APPS.md. */
#include <windows.h>
#include <signal.h>
#include <stdlib.h>

static __neo98_signal_handler handlers[NSIG];

__neo98_signal_handler signal(int number, __neo98_signal_handler handler)
{
    __neo98_signal_handler previous;
    if (number != SIGINT && number != SIGILL && number != SIGFPE && number != SIGSEGV && number != SIGTERM &&
        number != SIGBREAK && number != SIGABRT)
        return SIG_ERR;
    previous = handlers[number];
    handlers[number] = handler;
    return previous;
}
/* Calls the handler of the program; the action goes back to the default
   first, as the standard has it. Nonzero when a handler ran or the signal is
   ignored. */
static int Deliver(int number)
{
    __neo98_signal_handler handler = handlers[number];
    if (handler == SIG_IGN) return 1;
    if (handler == SIG_DFL) return 0;
    handlers[number] = SIG_DFL;
    handler(number);
    return 1;
}
int raise(int number)
{
    if (number <= 0 || number >= NSIG) return -1;
    if (Deliver(number)) return 0;
    /* The default action of every signal ends the program. */
    _exit(3);
}
int __neo98_raise_abort(void) { return Deliver(SIGABRT); }

/* The filter of the process, until the program sets its own. Two things, as
   the runtime of MinGW does them:
   - An exception of the C++ runtime that no frame catches (the throw of
     libunwind, code 20474343h) continues: RaiseException returns to the
     runtime, which calls std::terminate.
   - A fault of the processor goes to the signal handler of the program, if
     it has one; the program continues at the fault when the handler returns.
   Everything else is left to the system (the error box). */
LONG WINAPI __neo98_unhandled_filter(struct _EXCEPTION_POINTERS *pointers)
{
    const EXCEPTION_RECORD *record = pointers->ExceptionRecord;
    int number = 0;
    if ((record->ExceptionCode & 0x20ffffff) == 0x20474343 && !(record->ExceptionFlags & EXCEPTION_NONCONTINUABLE))
        return EXCEPTION_CONTINUE_EXECUTION;
    switch (record->ExceptionCode)
    {
    case EXCEPTION_ACCESS_VIOLATION: number = SIGSEGV; break;
    case EXCEPTION_ILLEGAL_INSTRUCTION: number = SIGILL; break;
    case EXCEPTION_INT_DIVIDE_BY_ZERO: number = SIGFPE; break;
    default: return EXCEPTION_CONTINUE_SEARCH;
    }
    return Deliver(number) ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}
