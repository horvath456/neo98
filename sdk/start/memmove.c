/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* What the compiler calls by itself; sdk/start. One function per file, so a
   program that defines its own keeps it. docs/ARM64-APPS.md. */
typedef unsigned long long size_t;

void *memmove(void *destination, const void *source, size_t count)
{
    unsigned char *d = (unsigned char *)destination;
    const unsigned char *s = (const unsigned char *)source;
    if (d < s)
        while (count--) *d++ = *s++;
    else
        while (count--) d[count] = s[count];
    return destination;
}
