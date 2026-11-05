/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* The static C runtime of the NEO98 SDK (sdk/crt): wide characters. The
   functions are those of musl. wchar_t has 16 bits, as under Windows; musl
   is written for 32. UTF-8 scalars beyond U+FFFF are rejected with EILSEQ
   because they cannot fit in one Windows wchar_t. In the "C"
   locale a multibyte character is one byte: below 128 it is the ASCII
   character, above it is a byte without a meaning, which converts to a wide
   character of its own (DF80h to DFFFh) and back. In the UTF-8 locale
   (setlocale) it is UTF-8. The code pages of Windows are not here; a program
   that needs them calls MultiByteToWideChar. docs/ARM64-APPS.md. */
#ifndef NEO98_CRT_WCHAR_H
#define NEO98_CRT_WCHAR_H
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>
#include <wctype.h>
#ifdef __cplusplus
extern "C" {
#endif
#ifndef NEO98_CRT_WINT_T
#define NEO98_CRT_WINT_T
typedef unsigned wint_t;
typedef unsigned long wctype_t;
#endif
#ifndef NEO98_CRT_MBSTATE_T
#define NEO98_CRT_MBSTATE_T
typedef struct __mbstate_t { unsigned __opaque1, __opaque2; } mbstate_t;
#endif
#ifndef WEOF
#define WEOF 0xffffffffU
#endif
#ifndef WCHAR_MIN
#define WCHAR_MIN 0
#define WCHAR_MAX 0xffff
#endif
wchar_t *wcscpy(wchar_t *, const wchar_t *);
wchar_t *wcsncpy(wchar_t *, const wchar_t *, size_t);
wchar_t *wcpcpy(wchar_t *, const wchar_t *);
wchar_t *wcpncpy(wchar_t *, const wchar_t *, size_t);
wchar_t *wcscat(wchar_t *, const wchar_t *);
wchar_t *wcsncat(wchar_t *, const wchar_t *, size_t);
int wcscmp(const wchar_t *, const wchar_t *);
int wcsncmp(const wchar_t *, const wchar_t *, size_t);
int wcscoll(const wchar_t *, const wchar_t *);
size_t wcsxfrm(wchar_t *, const wchar_t *, size_t);
wchar_t *wcschr(const wchar_t *, wchar_t);
wchar_t *wcsrchr(const wchar_t *, wchar_t);
wchar_t *wcsstr(const wchar_t *, const wchar_t *);
wchar_t *wcspbrk(const wchar_t *, const wchar_t *);
size_t wcsspn(const wchar_t *, const wchar_t *);
size_t wcscspn(const wchar_t *, const wchar_t *);
wchar_t *wcstok(wchar_t *, const wchar_t *, wchar_t **);
size_t wcslen(const wchar_t *);
size_t wcsnlen(const wchar_t *, size_t);
wchar_t *wcsdup(const wchar_t *);
wchar_t *wmemchr(const wchar_t *, wchar_t, size_t);
int wmemcmp(const wchar_t *, const wchar_t *, size_t);
wchar_t *wmemcpy(wchar_t *, const wchar_t *, size_t);
wchar_t *wmemmove(wchar_t *, const wchar_t *, size_t);
wchar_t *wmemset(wchar_t *, wchar_t, size_t);
long wcstol(const wchar_t *, wchar_t **, int);
unsigned long wcstoul(const wchar_t *, wchar_t **, int);
long long wcstoll(const wchar_t *, wchar_t **, int);
unsigned long long wcstoull(const wchar_t *, wchar_t **, int);
double wcstod(const wchar_t *, wchar_t **);
float wcstof(const wchar_t *, wchar_t **);
long double wcstold(const wchar_t *, wchar_t **);
wint_t btowc(int);
int wctob(wint_t);
int mbsinit(const mbstate_t *);
size_t mbrlen(const char *, size_t, mbstate_t *);
size_t mbrtowc(wchar_t *, const char *, size_t, mbstate_t *);
size_t wcrtomb(char *, wchar_t, mbstate_t *);
size_t mbsrtowcs(wchar_t *, const char **, size_t, mbstate_t *);
size_t wcsrtombs(char *, const wchar_t **, size_t, mbstate_t *);
size_t mbsnrtowcs(wchar_t *, const char **, size_t, size_t, mbstate_t *);
size_t wcsnrtombs(char *, const wchar_t **, size_t, size_t, mbstate_t *);
wint_t fgetwc(FILE *);
wint_t getwc(FILE *);
wint_t getwchar(void);
wint_t fputwc(wchar_t, FILE *);
wint_t putwc(wchar_t, FILE *);
wint_t putwchar(wchar_t);
wchar_t *fgetws(wchar_t *, int, FILE *);
int fputws(const wchar_t *, FILE *);
wint_t ungetwc(wint_t, FILE *);
int fwide(FILE *, int);
int swprintf(wchar_t *, size_t, const wchar_t *, ...);
int vswprintf(wchar_t *, size_t, const wchar_t *, va_list);
int fwprintf(FILE *, const wchar_t *, ...);
int vfwprintf(FILE *, const wchar_t *, va_list);
int wprintf(const wchar_t *, ...);
int vwprintf(const wchar_t *, va_list);
int swscanf(const wchar_t *, const wchar_t *, ...);
int vswscanf(const wchar_t *, const wchar_t *, va_list);
int fwscanf(FILE *, const wchar_t *, ...);
int vfwscanf(FILE *, const wchar_t *, va_list);
int wscanf(const wchar_t *, ...);
int vwscanf(const wchar_t *, va_list);
size_t wcsftime(wchar_t *, size_t, const wchar_t *, const struct tm *);
/* Names of the Windows runtime. */
int _wcsicmp(const wchar_t *, const wchar_t *);
int _wcsnicmp(const wchar_t *, const wchar_t *, size_t);
wchar_t *_wcsdup(const wchar_t *);
#ifdef __cplusplus
}
#endif
#endif
