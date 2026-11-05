/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* NEO98: the types musl's headers take from <bits/alltypes.h>, for the
   headers of musl that are used as they are (math.h, langinfo.h,
   nl_types.h). Not part of musl. */
#if defined(__NEED_float_t) && !defined(__DEFINED_float_t)
typedef float float_t;
#define __DEFINED_float_t
#endif
#if defined(__NEED_double_t) && !defined(__DEFINED_double_t)
typedef double double_t;
#define __DEFINED_double_t
#endif
#if defined(__NEED_locale_t) && !defined(__DEFINED_locale_t)
#include <locale.h>
#define __DEFINED_locale_t
#endif
