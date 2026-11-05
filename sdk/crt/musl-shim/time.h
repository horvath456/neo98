/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* NEO98: musl's src/include/time.h. Not part of musl. */
#ifndef NEO98_MUSL_TIME_H
#define NEO98_MUSL_TIME_H
#include_next <time.h>
#include <locale.h>
struct tm *__gmtime_r(const time_t *, struct tm *);
char *__asctime_r(const struct tm *, char *);
size_t __strftime_l(char *, size_t, const char *, const struct tm *, struct __locale_struct *);
size_t __wcsftime_l(wchar_t *, size_t, const wchar_t *, const struct tm *, struct __locale_struct *);
size_t strftime_l(char *, size_t, const char *, const struct tm *, struct __locale_struct *);
size_t wcsftime_l(wchar_t *, size_t, const wchar_t *, const struct tm *, struct __locale_struct *);
#endif
