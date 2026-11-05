/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* The static C runtime of the NEO98 SDK (sdk/crt): threads under the names
   of the Windows runtime. The runtime is static and keeps nothing per
   thread that would have to be made or freed, so these are CreateThread and
   ExitThread of KERNEL32. */
#ifndef NEO98_CRT_PROCESS_H
#define NEO98_CRT_PROCESS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* The handle of the thread, or 0 with errno set. The caller closes it. */
uintptr_t _beginthreadex(void *security, unsigned stack_size, unsigned (__stdcall *start)(void *), void *argument,
                         unsigned flags, unsigned *thread_id);
void _endthreadex(unsigned code);
/* The handle, or -1; it is closed when the thread ends. */
uintptr_t _beginthread(void (__cdecl *start)(void *), unsigned stack_size, void *argument);
void _endthread(void);
int _getpid(void);
#define getpid _getpid
#ifdef __cplusplus
}
#endif
#endif
