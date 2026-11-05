/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* The static C runtime of the NEO98 SDK: the clocks of KERNEL32 and the
   offset of local time. The calendar arithmetic (__secs_to_tm,
   __tm_to_secs) and strftime are musl's; musl takes local time from zone
   files, which a Windows has not, so the two functions that need the zone
   are here. Own code. docs/ARM64-APPS.md. */
#include <windows.h>
#include <errno.h>
#include <time.h>
#include "time_impl.h"

/* 100 ns units between 1601 and 1970. */
#define EPOCH_DIFFERENCE 116444736000000000ull
/* musl's name for the zone of gmtime. */
const char __utc[] = "UTC";

time_t time(time_t *out)
{
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    const unsigned long long ticks = (unsigned long long)now.dwHighDateTime << 32 | now.dwLowDateTime;
    const time_t seconds = (time_t)((ticks - EPOCH_DIFFERENCE) / 10000000ull);
    if (out) *out = seconds;
    return seconds;
}
clock_t clock(void) { return (clock_t)GetTickCount(); }
int timespec_get(struct timespec *out, int base)
{
    FILETIME now;
    if (base != TIME_UTC) return 0;
    GetSystemTimeAsFileTime(&now);
    const unsigned long long ticks = ((unsigned long long)now.dwHighDateTime << 32 | now.dwLowDateTime) - EPOCH_DIFFERENCE;
    out->tv_sec = (time_t)(ticks / 10000000ull);
    out->tv_nsec = (long)(ticks % 10000000ull) * 100;
    return base;
}
/* The offset of local time from UTC at a moment, in seconds: what the
   system adds when it converts that moment. */
static long long LocalOffset(long long seconds)
{
    const unsigned long long ticks = (unsigned long long)seconds * 10000000ull + EPOCH_DIFFERENCE;
    FILETIME utc, local;
    utc.dwLowDateTime = (DWORD)ticks;
    utc.dwHighDateTime = (DWORD)(ticks >> 32);
    if (!FileTimeToLocalFileTime(&utc, &local)) return 0;
    const long long a = (long long)ticks;
    const long long b = (long long)((unsigned long long)local.dwHighDateTime << 32 | local.dwLowDateTime);
    return (b - a) / 10000000;
}
struct tm *localtime_r(const time_t *when, struct tm *out)
{
    const long long offset = LocalOffset(*when);
    if (__secs_to_tm(*when + offset, out) < 0)
    {
        errno = EOVERFLOW;
        return NULL;
    }
    out->tm_isdst = -1;
    out->__tm_gmtoff = (long)offset;
    out->__tm_zone = "";
    return out;
}
struct tm *localtime(const time_t *when)
{
    static struct tm result;
    return localtime_r(when, &result);
}
time_t mktime(struct tm *value)
{
    /* The fields as a moment of UTC, then back by the offset of local time
       at about that moment. */
    const long long local = __tm_to_secs(value);
    long long seconds = local - LocalOffset(local);
    seconds = local - LocalOffset(seconds);
    if (!localtime_r(&seconds, value)) return (time_t)-1;
    return (time_t)seconds;
}
char *ctime(const time_t *when)
{
    struct tm *value = localtime(when);
    return value ? asctime(value) : NULL;
}
/* The name of the zone for %Z of strftime: the runtime knows the offset of
   local time and no name for it. */
const char *__tm_to_tzname(const struct tm *value)
{
    return value->__tm_zone ? value->__tm_zone : "";
}
