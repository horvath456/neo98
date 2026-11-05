/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* NEO98: the functions with a locale argument that the sources of musl
   define beside the plain ones. Not part of musl. */
#ifndef NEO98_MUSL_WCTYPE_H
#define NEO98_MUSL_WCTYPE_H
#include_next <wctype.h>
#include <locale.h>
struct __locale_struct;
#define NEO98_WIDE_L(name) int __##name##_l(wint_t, struct __locale_struct *); int name##_l(wint_t, struct __locale_struct *);
NEO98_WIDE_L(iswalnum) NEO98_WIDE_L(iswalpha) NEO98_WIDE_L(iswblank) NEO98_WIDE_L(iswcntrl) NEO98_WIDE_L(iswdigit)
NEO98_WIDE_L(iswgraph) NEO98_WIDE_L(iswlower) NEO98_WIDE_L(iswprint) NEO98_WIDE_L(iswpunct) NEO98_WIDE_L(iswspace)
NEO98_WIDE_L(iswupper) NEO98_WIDE_L(iswxdigit)
int __iswctype_l(wint_t, wctype_t, struct __locale_struct *);
int iswctype_l(wint_t, wctype_t, struct __locale_struct *);
wctype_t __wctype_l(const char *, struct __locale_struct *);
wctype_t wctype_l(const char *, struct __locale_struct *);
wint_t __towlower_l(wint_t, struct __locale_struct *);
wint_t __towupper_l(wint_t, struct __locale_struct *);
wint_t towlower_l(wint_t, struct __locale_struct *);
wint_t towupper_l(wint_t, struct __locale_struct *);
wint_t __towctrans_l(wint_t, wctrans_t, struct __locale_struct *);
wint_t towctrans_l(wint_t, wctrans_t, struct __locale_struct *);
wctrans_t __wctrans_l(const char *, struct __locale_struct *);
wctrans_t wctrans_l(const char *, struct __locale_struct *);
#endif
