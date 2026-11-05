/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* NEO98: musl's src/include/string.h: the internal names of functions the
   imported sources call each other by. Not part of musl. */
#ifndef NEO98_MUSL_STRING_H
#define NEO98_MUSL_STRING_H
#include_next <string.h>
void *__memrchr(const void *, int, size_t);
char *__stpcpy(char *, const char *);
char *__stpncpy(char *, const char *, size_t);
char *__strchrnul(const char *, int);
#endif
