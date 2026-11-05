/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* The static C runtime of the NEO98 SDK: the locale of the process and the
   names of the Windows runtime for functions with a locale argument. The
   locale objects ("C" and the UTF-8 one) and everything that depends on
   them are musl's. musl finds a locale by name in files and in the
   environment; here there are the two it has built in, chosen by name. Own
   code. docs/ARM64-APPS.md. */
#include <ctype.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>
#include "locale_impl.h"

/* The categories as a program names them: the numbers of the Windows
   runtime (<locale.h> of sdk/crt). In this file LC_CTYPE and the others are
   musl's numbers, the indices of its locale object (musl-shim/locale.h). */
enum { WindowsAll = 0, WindowsCtype = 2, WindowsMax = 5 };

/* 1: "C". 2: UTF-8. 0: no such locale. */
static int Known(const char *name)
{
    size_t length;
    if (!*name || !strcmp(name, "C") || !strcmp(name, "POSIX")) return 1;
    length = strlen(name);
    if (length >= 6 && (!strcmp(name + length - 6, ".UTF-8") || !strcmp(name + length - 6, ".utf-8"))) return 2;
    if (length >= 5 && (!strcmp(name + length - 5, ".UTF8") || !strcmp(name + length - 5, ".utf8"))) return 2;
    return 0;
}
char *setlocale(int category, const char *name)
{
    static char c_name[] = "C", utf8_name[] = "C.UTF-8";
    const int ctype = category == WindowsAll || category == WindowsCtype;
    if (category < WindowsAll || category > WindowsMax) return NULL;
    if (name)
    {
        const int which = Known(name);
        if (!which) return NULL;
        /* Only what a multibyte character is differs between the two. */
        if (ctype) libc.global_locale.cat[LC_CTYPE] = which == 2 ? &__c_dot_utf8 : 0;
    }
    return ctype && libc.global_locale.cat[LC_CTYPE] ? utf8_name : c_name;
}
_locale_t _create_locale(int category, const char *name)
{
    const int which = category < WindowsAll || category > WindowsMax ? 0 : Known(name);
    if (!which) return NULL;
    return which == 2 && (category == WindowsAll || category == WindowsCtype) ? UTF8_LOCALE : C_LOCALE;
}
void _free_locale(_locale_t locale) { (void)locale; }
_locale_t _get_current_locale(void) { return libc.global_locale.cat[LC_CTYPE] ? UTF8_LOCALE : C_LOCALE; }
int _configthreadlocale(int how)
{
    (void)how;
    return _DISABLE_PER_THREAD_LOCALE;
}
int ___mb_cur_max_func(void) { return (int)MB_CUR_MAX; }
int ___mb_cur_max_l_func(_locale_t locale) { return locale && locale->cat[LC_CTYPE] ? 4 : 1; }
/* The code page of the locale as the Windows runtime numbers it: none for
   "C", 65001 for UTF-8. */
unsigned ___lc_codepage_func(void) { return libc.global_locale.cat[LC_CTYPE] ? 65001 : 0; }

/* The table of class bits the Windows runtime hands out, for the characters
   -1 to 255, from the classes of musl. */
const unsigned short *__pctype_func(void)
{
    static unsigned short table[257];
    static int ready;
    if (!ready)
    {
        for (int c = 0; c < 256; ++c)
            table[c + 1] = (unsigned short)((isupper(c) ? _UPPER | 0x100 : 0) | (islower(c) ? _LOWER | 0x100 : 0) |
                                            (isdigit(c) ? _DIGIT : 0) | (isspace(c) ? _SPACE : 0) | (ispunct(c) ? _PUNCT : 0) |
                                            (iscntrl(c) ? _CONTROL : 0) | (c == ' ' ? _BLANK : 0) | (isxdigit(c) ? _HEX : 0));
        ready = 1;
    }
    return table + 1;
}
/* The class is a set of those bits. */
int _iswctype_l(wint_t c, wctype_t mask, _locale_t l)
{
    (void)l;
    return ((mask & _UPPER) && iswupper(c)) || ((mask & _LOWER) && iswlower(c)) || ((mask & 0x100) && iswalpha(c)) ||
           ((mask & _DIGIT) && iswdigit(c)) || ((mask & _SPACE) && iswspace(c)) || ((mask & _PUNCT) && iswpunct(c)) ||
           ((mask & _CONTROL) && iswcntrl(c)) || ((mask & _BLANK) && iswblank(c)) || ((mask & _HEX) && iswxdigit(c));
}

#define NARROW(name) int _##name##_l(int c, _locale_t l) { (void)l; return name(c); }
NARROW(isalnum) NARROW(isalpha) NARROW(iscntrl) NARROW(isdigit) NARROW(isgraph) NARROW(islower) NARROW(isprint)
NARROW(ispunct) NARROW(isspace) NARROW(isupper) NARROW(isxdigit) NARROW(isblank) NARROW(tolower) NARROW(toupper)
#define WIDE(name) int _##name##_l(wint_t c, _locale_t l) { (void)l; return name(c); }
WIDE(iswalnum) WIDE(iswalpha) WIDE(iswcntrl) WIDE(iswdigit) WIDE(iswgraph) WIDE(iswlower) WIDE(iswprint) WIDE(iswpunct)
WIDE(iswspace) WIDE(iswupper) WIDE(iswxdigit) WIDE(iswblank)
wint_t _towlower_l(wint_t c, _locale_t l) { (void)l; return towlower(c); }
wint_t _towupper_l(wint_t c, _locale_t l) { (void)l; return towupper(c); }

int _strcoll_l(const char *a, const char *b, _locale_t l) { (void)l; return strcoll(a, b); }
size_t _strxfrm_l(char *to, const char *from, size_t size, _locale_t l) { (void)l; return strxfrm(to, from, size); }
int _wcscoll_l(const wchar_t *a, const wchar_t *b, _locale_t l) { (void)l; return wcscoll(a, b); }
size_t _wcsxfrm_l(wchar_t *to, const wchar_t *from, size_t size, _locale_t l) { (void)l; return wcsxfrm(to, from, size); }
double _strtod_l(const char *text, char **end, _locale_t l) { (void)l; return strtod(text, end); }
float _strtof_l(const char *text, char **end, _locale_t l) { (void)l; return strtof(text, end); }
long double _strtold_l(const char *text, char **end, _locale_t l) { (void)l; return strtold(text, end); }
long long _strtoi64(const char *text, char **end, int base) { return strtoll(text, end, base); }
unsigned long long _strtoui64(const char *text, char **end, int base) { return strtoull(text, end, base); }
long long _strtoi64_l(const char *text, char **end, int base, _locale_t l) { (void)l; return strtoll(text, end, base); }
unsigned long long _strtoui64_l(const char *text, char **end, int base, _locale_t l) { (void)l; return strtoull(text, end, base); }
int _mbtowc_l(wchar_t *out, const char *text, size_t size, _locale_t l) { (void)l; return mbtowc(out, text, size); }
size_t _strftime_l(char *buffer, size_t size, const char *format, const struct tm *value, _locale_t l)
{
    (void)l;
    return strftime(buffer, size, format, value);
}
