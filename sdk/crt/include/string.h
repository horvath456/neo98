/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* The static C runtime of the NEO98 SDK (sdk/crt). The functions are those
   of musl; memcpy, memmove, memset and memcmp are the compiler's helpers in
   sdk/start; the names of the Windows runtime and the texts of its error
   numbers are own code. docs/ARM64-APPS.md. */
#ifndef NEO98_CRT_STRING_H
#define NEO98_CRT_STRING_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#ifndef NEO98_CRT_LOCALE_T
#define NEO98_CRT_LOCALE_T
typedef struct __locale_struct *_locale_t;
#endif
void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
int memcmp(const void *, const void *, size_t);
void *memchr(const void *, int, size_t);
void *memrchr(const void *, int, size_t);
size_t strlen(const char *);
size_t strnlen(const char *, size_t);
char *strcpy(char *, const char *);
char *strncpy(char *, const char *, size_t);
char *stpcpy(char *, const char *);
char *stpncpy(char *, const char *, size_t);
char *strcat(char *, const char *);
char *strncat(char *, const char *, size_t);
int strcmp(const char *, const char *);
int strncmp(const char *, const char *, size_t);
int strcoll(const char *, const char *);
size_t strxfrm(char *, const char *, size_t);
char *strchr(const char *, int);
char *strchrnul(const char *, int);
char *strrchr(const char *, int);
char *strstr(const char *, const char *);
size_t strspn(const char *, const char *);
size_t strcspn(const char *, const char *);
char *strpbrk(const char *, const char *);
char *strtok(char *, const char *);
char *strtok_r(char *, const char *, char **);
char *strdup(const char *);
char *strndup(const char *, size_t);
char *strerror(int);
/* Names of the Windows runtime. */
int strerror_s(char *, size_t, int);
int _stricmp(const char *, const char *);
int _strnicmp(const char *, const char *, size_t);
char *_strdup(const char *);
#define stricmp _stricmp
#define strnicmp _strnicmp
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
int _strcoll_l(const char *, const char *, _locale_t);
size_t _strxfrm_l(char *, const char *, size_t, _locale_t);
int _wcscoll_l(const wchar_t *, const wchar_t *, _locale_t);
size_t _wcsxfrm_l(wchar_t *, const wchar_t *, size_t, _locale_t);
#ifdef __cplusplus
}
#endif
#endif
