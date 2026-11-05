/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* The static C runtime of the NEO98 SDK (sdk/crt). Numbers from text,
   sorting, searching and the allocator are those of musl; the end of the
   program and the environment go to KERNEL32. */
#ifndef NEO98_CRT_STDLIB_H
#define NEO98_CRT_STDLIB_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
/* rand is the generator of the Windows runtime, with its range. */
#define RAND_MAX 0x7fff
#ifndef NEO98_CRT_ERRNO_T
#define NEO98_CRT_ERRNO_T
typedef int errno_t;
#endif
#ifndef NEO98_CRT_LOCALE_T
#define NEO98_CRT_LOCALE_T
typedef struct __locale_struct *_locale_t;
#endif
/* The version of the Windows runtime this one stands for, for sources that
   ask (the headers of MinGW define it). */
#ifndef __MSVCRT_VERSION__
#define __MSVCRT_VERSION__ 0x0E00
#endif
/* Bytes of the longest multibyte character of the locale: 1 for "C", 4 for
   UTF-8. */
size_t __ctype_get_mb_cur_max(void);
#define MB_CUR_MAX (__ctype_get_mb_cur_max())
typedef struct { int quot, rem; } div_t;
typedef struct { long quot, rem; } ldiv_t;
typedef struct { long long quot, rem; } lldiv_t;
/* The arguments of the program, also for one that starts in WinMain. */
extern int __argc;
extern char **__argv;
void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void free(void *);
/* C11. The block is freed with free, like any other. */
void *aligned_alloc(size_t alignment, size_t size);
/* The names of the Windows runtime. */
void *_aligned_malloc(size_t size, size_t alignment);
void _aligned_free(void *);
_Noreturn void exit(int);
_Noreturn void _exit(int);
_Noreturn void _Exit(int);
_Noreturn void quick_exit(int);
int at_quick_exit(void (*)(void));
_Noreturn void abort(void);
int atexit(void (*)(void));
char *getenv(const char *);
int system(const char *);
int atoi(const char *);
long atol(const char *);
long long atoll(const char *);
double atof(const char *);
long strtol(const char *, char **, int);
unsigned long strtoul(const char *, char **, int);
long long strtoll(const char *, char **, int);
unsigned long long strtoull(const char *, char **, int);
double strtod(const char *, char **);
float strtof(const char *, char **);
long double strtold(const char *, char **);
int abs(int);
long labs(long);
long long llabs(long long);
div_t div(int, int);
ldiv_t ldiv(long, long);
lldiv_t lldiv(long long, long long);
int rand(void);
void srand(unsigned);
void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
void *bsearch(const void *, const void *, size_t, size_t, int (*)(const void *, const void *));
int mblen(const char *, size_t);
int mbtowc(wchar_t *, const char *, size_t);
int wctomb(char *, wchar_t);
size_t mbstowcs(wchar_t *, const char *, size_t);
size_t wcstombs(char *, const wchar_t *, size_t);
/* Names of the Windows runtime. */
double _strtod_l(const char *, char **, _locale_t);
float _strtof_l(const char *, char **, _locale_t);
long double _strtold_l(const char *, char **, _locale_t);
long long _strtoi64(const char *, char **, int);
unsigned long long _strtoui64(const char *, char **, int);
long long _strtoi64_l(const char *, char **, int, _locale_t);
unsigned long long _strtoui64_l(const char *, char **, int, _locale_t);
int _mbtowc_l(wchar_t *, const char *, size_t, _locale_t);
int ___mb_cur_max_func(void);
/* A random number of the system; zero for success. */
int rand_s(unsigned int *);
#ifdef __cplusplus
}
#endif
#endif
