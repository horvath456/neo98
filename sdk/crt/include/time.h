/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* The static C runtime of the NEO98 SDK (sdk/crt). The calendar arithmetic
   and strftime are those of musl; the clocks and the offset of local time
   come from KERNEL32. */
#ifndef NEO98_CRT_TIME_H
#define NEO98_CRT_TIME_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef long long time_t;
typedef long clock_t;
#define CLOCKS_PER_SEC 1000
/* The two members after tm_isdst are musl's: the offset from UTC in seconds
   and the name of the zone, which strftime prints for %z and %Z. */
struct tm
{
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst;
    long __tm_gmtoff;
    const char *__tm_zone;
};
struct timespec { time_t tv_sec; long tv_nsec; };
#define TIME_UTC 1
#ifndef NEO98_CRT_LOCALE_T
#define NEO98_CRT_LOCALE_T
typedef struct __locale_struct *_locale_t;
#endif
time_t time(time_t *);
clock_t clock(void);
double difftime(time_t, time_t);
struct tm *gmtime(const time_t *);
struct tm *gmtime_r(const time_t *, struct tm *);
struct tm *localtime(const time_t *);
struct tm *localtime_r(const time_t *, struct tm *);
time_t mktime(struct tm *);
time_t timegm(struct tm *);
char *asctime(const struct tm *);
char *asctime_r(const struct tm *, char *);
char *ctime(const time_t *);
size_t strftime(char *, size_t, const char *, const struct tm *);
int timespec_get(struct timespec *, int);
/* A name of the Windows runtime. */
size_t _strftime_l(char *, size_t, const char *, const struct tm *, _locale_t);
#ifdef __cplusplus
}
#endif
#endif
