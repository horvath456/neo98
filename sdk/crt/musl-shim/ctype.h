/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* NEO98: the functions with a locale argument that the sources of musl
   define beside the plain ones. Not part of musl. */
#ifndef NEO98_MUSL_CTYPE_H
#define NEO98_MUSL_CTYPE_H
#include_next <ctype.h>
#include <locale.h>
struct __locale_struct;
#define NEO98_NARROW_L(name) int __##name##_l(int, struct __locale_struct *); int name##_l(int, struct __locale_struct *);
NEO98_NARROW_L(isalnum) NEO98_NARROW_L(isalpha) NEO98_NARROW_L(isblank) NEO98_NARROW_L(iscntrl) NEO98_NARROW_L(isdigit)
NEO98_NARROW_L(isgraph) NEO98_NARROW_L(islower) NEO98_NARROW_L(isprint) NEO98_NARROW_L(ispunct) NEO98_NARROW_L(isspace)
NEO98_NARROW_L(isupper) NEO98_NARROW_L(isxdigit) NEO98_NARROW_L(tolower) NEO98_NARROW_L(toupper)
int isascii(int);
int toascii(int);
#endif
