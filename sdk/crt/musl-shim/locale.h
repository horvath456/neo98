/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* NEO98: what the sources of musl need of <locale.h> beyond the runtime's
   own header: its locale type, and its own numbers for the categories. musl
   uses a category as an index into its locale object and as the upper half
   of an nl_langinfo item (langinfo.h), so its sources must see musl's
   numbers; a program sees those of the Windows runtime, and
   sdk/crt/src/locale.c, the one place where both meet, names the latter
   itself. Not part of musl. */
#ifndef NEO98_MUSL_LOCALE_H
#define NEO98_MUSL_LOCALE_H
#include_next <locale.h>
#undef LC_ALL
#undef LC_COLLATE
#undef LC_CTYPE
#undef LC_MONETARY
#undef LC_NUMERIC
#undef LC_TIME
#define LC_CTYPE 0
#define LC_NUMERIC 1
#define LC_TIME 2
#define LC_COLLATE 3
#define LC_MONETARY 4
#define LC_MESSAGES 5
#define LC_ALL 6
typedef struct __locale_struct *locale_t;
int __strcoll_l(const char *, const char *, locale_t);
size_t __strxfrm_l(char *, const char *, size_t, locale_t);
int strcoll_l(const char *, const char *, locale_t);
size_t strxfrm_l(char *, const char *, size_t, locale_t);
char *__nl_langinfo_l(int, locale_t);
char *__nl_langinfo(int);
#endif
