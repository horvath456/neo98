/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* The static C runtime of the NEO98 SDK (sdk/crt): signals. raise calls the
   handler of the program; a fault of the processor nobody handled as an
   exception reaches the handler of SIGSEGV, SIGILL or SIGFPE through the
   filter the start code installs (SetUnhandledExceptionFilter). */
#ifndef NEO98_CRT_SIGNAL_H
#define NEO98_CRT_SIGNAL_H
#ifdef __cplusplus
extern "C" {
#endif
typedef int sig_atomic_t;
#define SIGINT 2
#define SIGILL 4
#define SIGFPE 8
#define SIGSEGV 11
#define SIGTERM 15
#define SIGBREAK 21
#define SIGABRT 22
#define NSIG 23
typedef void (*__neo98_signal_handler)(int);
#define SIG_DFL ((__neo98_signal_handler)0)
#define SIG_IGN ((__neo98_signal_handler)1)
#define SIG_ERR ((__neo98_signal_handler)-1)
__neo98_signal_handler signal(int, __neo98_signal_handler);
int raise(int);
#ifdef __cplusplus
}
#endif
#endif
