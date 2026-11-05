/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* NEO98: the functions with a locale argument that the sources of musl
   define beside the plain ones. Not part of musl. */
#ifndef NEO98_MUSL_WCHAR_H
#define NEO98_MUSL_WCHAR_H
#include_next <wchar.h>
#include <locale.h>
struct __locale_struct;
int __wcscoll_l(const wchar_t *, const wchar_t *, struct __locale_struct *);
size_t __wcsxfrm_l(wchar_t *, const wchar_t *, size_t, struct __locale_struct *);
int wcscoll_l(const wchar_t *, const wchar_t *, struct __locale_struct *);
size_t wcsxfrm_l(wchar_t *, const wchar_t *, size_t, struct __locale_struct *);
#endif
