/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* What the compiler calls by itself; sdk/start. One function per file, so a
   program that defines its own keeps it. docs/ARM64-APPS.md. */
typedef unsigned long long size_t;

int memcmp(const void *left, const void *right, size_t count)
{
    const unsigned char *a = (const unsigned char *)left, *b = (const unsigned char *)right;
    for (; count--; ++a, ++b)
        if (*a != *b) return *a < *b ? -1 : 1;
    return 0;
}
