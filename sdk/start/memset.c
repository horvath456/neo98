/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* What the compiler calls by itself; sdk/start. One function per file, so a
   program that defines its own keeps it. docs/ARM64-APPS.md. */
typedef unsigned long long size_t;

void *memset(void *destination, int value, size_t count)
{
    unsigned char *p = (unsigned char *)destination;
    while (count--) *p++ = (unsigned char)value;
    return destination;
}
