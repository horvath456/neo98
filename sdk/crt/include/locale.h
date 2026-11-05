/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* The static C runtime of the NEO98 SDK (sdk/crt): locales. There are two,
   "C" and "C.UTF-8", which differ in what a multibyte character is
   (<wchar.h>); texts, numbers and dates are those of "C" in both. setlocale
   and _create_locale accept "C", "POSIX", "" (the locale of the user, which
   is "C" here) and a name that ends in ".UTF-8" or ".utf8", and refuse every
   other name. The locale is one for the process. The functions with a
   locale argument, as the Windows runtime names them (_strtod_l and the
   like), act for "C". */
#ifndef NEO98_CRT_LOCALE_H
#define NEO98_CRT_LOCALE_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define LC_ALL 0
#define LC_COLLATE 1
#define LC_CTYPE 2
#define LC_MONETARY 3
#define LC_NUMERIC 4
#define LC_TIME 5
#define LC_MIN LC_ALL
#define LC_MAX LC_TIME
struct lconv
{
    char *decimal_point, *thousands_sep, *grouping, *int_curr_symbol, *currency_symbol, *mon_decimal_point,
         *mon_thousands_sep, *mon_grouping, *positive_sign, *negative_sign;
    char int_frac_digits, frac_digits, p_cs_precedes, p_sep_by_space, n_cs_precedes, n_sep_by_space, p_sign_posn,
         n_sign_posn, int_p_cs_precedes, int_p_sep_by_space, int_n_cs_precedes, int_n_sep_by_space, int_p_sign_posn,
         int_n_sign_posn;
};
char *setlocale(int, const char *);
struct lconv *localeconv(void);
#ifndef NEO98_CRT_LOCALE_T
#define NEO98_CRT_LOCALE_T
typedef struct __locale_struct *_locale_t;
#endif
_locale_t _create_locale(int, const char *);
void _free_locale(_locale_t);
_locale_t _get_current_locale(void);
#define _ENABLE_PER_THREAD_LOCALE 1
#define _DISABLE_PER_THREAD_LOCALE 2
int _configthreadlocale(int);
int ___mb_cur_max_func(void);
int ___mb_cur_max_l_func(_locale_t);
unsigned ___lc_codepage_func(void);
#ifdef __cplusplus
}
#endif
#endif
