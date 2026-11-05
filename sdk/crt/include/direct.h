/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* The static C runtime of the NEO98 SDK (sdk/crt): directories under the
   names of the Windows runtime, over KERNEL32. 0, or -1 with errno set. */
#ifndef NEO98_CRT_DIRECT_H
#define NEO98_CRT_DIRECT_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int _mkdir(const char *);
int _rmdir(const char *);
int _chdir(const char *);
/* The current directory; with no buffer, one from malloc. */
char *_getcwd(char *, int);
#define rmdir _rmdir
#define chdir _chdir
#define getcwd _getcwd
#ifdef __cplusplus
}
#endif
#endif
