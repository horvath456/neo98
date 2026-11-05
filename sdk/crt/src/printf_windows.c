/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* The static C runtime of the NEO98 SDK: what printf of the Windows runtime
   has beyond the standard. The formatter is musl's vfprintf, built under
   the name __musl_vfprintf; every member of the printf family ends here.
   This function rewrites two things in the format and hands it on:

     %I64d, %I32d, %Id the lengths of the Windows runtime, to %lld, %d and
                         %lld
     %p sixteen upper-case hexadecimal digits without a
                         prefix, as the Windows runtime prints a pointer

   A format without either goes through untouched. Own code.
   docs/ARM64-APPS.md. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int __musl_vfprintf(FILE *, const char *, va_list);

/* Zero when nothing is to rewrite; else the length of the rewritten format,
   which goes to `out` when there is one. */
static size_t Rewrite(const char *format, char *out)
{
    size_t n = 0;
    int changed = 0;
    for (const char *p = format; *p; ++p)
    {
        int precision = 0;
        if (out) out[n] = *p;
        ++n;
        if (*p != '%') continue;
        if (p[1] == '%')
        {
            if (out) out[n] = '%';
            ++n;
            ++p;
            continue;
        }
        /* flags, width, precision, also as a numbered argument */
        for (++p; *p && strchr("-+ #0'*$.0123456789", *p); ++p)
        {
            if (*p == '.') precision = 1;
            if (out) out[n] = *p;
            ++n;
        }
        if (p[0] == 'I' && (p[1] == '6' || p[1] == '3' || strchr("diouxX", p[1])))
        {
            const int wide = !(p[1] == '3' && p[2] == '2');
            if (p[1] == '6' && p[2] == '4') p += 3;
            else if (p[1] == '3' && p[2] == '2') p += 3;
            else p += 1;
            if (wide)
            {
                if (out) memcpy(out + n, "ll", 2);
                n += 2;
            }
            changed = 1;
        }
        if (*p == 'p')
        {
            const char *as = precision ? "llX" : ".16llX";
            if (out) memcpy(out + n, as, strlen(as));
            n += strlen(as);
            changed = 1;
            continue;
        }
        if (!*p) break;
        if (out) out[n] = *p;
        ++n;
    }
    if (out) out[n] = 0;
    return changed ? n : 0;
}
int vfprintf(FILE *stream, const char *format, va_list arguments)
{
    char local[256], *rewritten = local;
    const size_t length = Rewrite(format, NULL);
    int result;
    if (!length) return __musl_vfprintf(stream, format, arguments);
    if (length >= sizeof local && !(rewritten = (char *)malloc(length + 1))) return -1;
    Rewrite(format, rewritten);
    result = __musl_vfprintf(stream, rewritten, arguments);
    if (rewritten != local) free(rewritten);
    return result;
}
