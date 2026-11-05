/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* The static C runtime of the NEO98 SDK (sdk/crt): the numbers of streams, as
   the Windows runtime has them. A stream has a number; the low-level calls
   on numbers (_open, _read, _write) are not built. */
#ifndef NEO98_CRT_IO_H
#define NEO98_CRT_IO_H
#include <stdint.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
/* The handle of KERNEL32 behind the stream with this number, or -1. */
intptr_t _get_osfhandle(int);
int _open_osfhandle(intptr_t, int);
int _close(int);
/* Whether the stream with this number is a console. */
int _isatty(int);
/* Whether a file is there (mode 0) and may be written (mode 2): 0, or -1
   with errno set. */
int _access(const char *, int);
#define access _access
int _unlink(const char *);
#define unlink _unlink
#ifdef __cplusplus
}
#endif
#endif
