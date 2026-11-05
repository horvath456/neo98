/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* The static C runtime of the NEO98 SDK (sdk/crt): classes of characters in
   the "C" locale, from musl; the names, the class bits and the table of the
   Windows runtime beside them (sdk/crt/src/locale.c). */
#ifndef NEO98_CRT_CTYPE_H
#define NEO98_CRT_CTYPE_H
#ifdef __cplusplus
extern "C" {
#endif
int isalnum(int); int isalpha(int); int iscntrl(int); int isdigit(int); int isgraph(int); int islower(int);
int isprint(int); int ispunct(int); int isspace(int); int isupper(int); int isxdigit(int); int isblank(int);
int tolower(int); int toupper(int);
/* The Windows runtime's forms without a test: the argument is a capital
   letter, or a small one. */
#define _tolower(c) ((c) - 'A' + 'a')
#define _toupper(c) ((c) - 'a' + 'A')
/* The bits of a class, the ones of the Windows runtime, and its table of
   them for the characters -1 to 255. */
#ifndef _UPPER
#define _UPPER 0x1
#define _LOWER 0x2
#define _DIGIT 0x4
#define _SPACE 0x8
#define _PUNCT 0x10
#define _CONTROL 0x20
#define _BLANK 0x40
#define _HEX 0x80
#define _ALPHA (0x0100 | _UPPER | _LOWER)
#endif
const unsigned short *__pctype_func(void);
#ifndef NEO98_CRT_LOCALE_T
#define NEO98_CRT_LOCALE_T
typedef struct __locale_struct *_locale_t;
#endif
int _isalnum_l(int, _locale_t); int _isalpha_l(int, _locale_t); int _iscntrl_l(int, _locale_t);
int _isdigit_l(int, _locale_t); int _isgraph_l(int, _locale_t); int _islower_l(int, _locale_t);
int _isprint_l(int, _locale_t); int _ispunct_l(int, _locale_t); int _isspace_l(int, _locale_t);
int _isupper_l(int, _locale_t); int _isxdigit_l(int, _locale_t); int _isblank_l(int, _locale_t);
int _tolower_l(int, _locale_t); int _toupper_l(int, _locale_t);
#ifndef NEO98_CRT_WINT_T
#define NEO98_CRT_WINT_T
typedef unsigned wint_t;
typedef unsigned long wctype_t;
#endif
int _iswalnum_l(wint_t, _locale_t); int _iswalpha_l(wint_t, _locale_t); int _iswcntrl_l(wint_t, _locale_t);
int _iswdigit_l(wint_t, _locale_t); int _iswgraph_l(wint_t, _locale_t); int _iswlower_l(wint_t, _locale_t);
int _iswprint_l(wint_t, _locale_t); int _iswpunct_l(wint_t, _locale_t); int _iswspace_l(wint_t, _locale_t);
int _iswupper_l(wint_t, _locale_t); int _iswxdigit_l(wint_t, _locale_t); int _iswblank_l(wint_t, _locale_t);
/* The class here is a set of the bits above, as the Windows runtime takes
   it, not what wctype answers. */
int _iswctype_l(wint_t, wctype_t, _locale_t);
wint_t _towlower_l(wint_t, _locale_t); wint_t _towupper_l(wint_t, _locale_t);
#ifdef __cplusplus
}
#endif
#endif
