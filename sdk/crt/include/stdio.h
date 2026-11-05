/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* The static C runtime of the NEO98 SDK (sdk/crt): streams. The stream layer
   and the printf and scanf families are those of musl; a stream reads,
   writes, seeks and closes through KERNEL32 (sdk/crt/src/stdio_windows.c).
   A stream opened without "b" is a text stream, as under Windows: a line
   ends with CR LF in the file and with a newline in the program. */
#ifndef NEO98_CRT_STDIO_H
#define NEO98_CRT_STDIO_H
#include <stdarg.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _IO_FILE FILE;
typedef long long fpos_t;
#ifndef NEO98_CRT_OFF_T
#define NEO98_CRT_OFF_T
typedef long long off_t;
#endif
#ifndef NEO98_CRT_LOCALE_T
#define NEO98_CRT_LOCALE_T
typedef struct __locale_struct *_locale_t;
#endif
#define EOF (-1)
#define BUFSIZ 1024
#define FILENAME_MAX 260
#define FOPEN_MAX 64
#define TMP_MAX 32767
#define L_tmpnam 260
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
extern FILE *const stdin;
extern FILE *const stdout;
extern FILE *const stderr;
#define stdin (stdin)
#define stdout (stdout)
#define stderr (stderr)
FILE *fopen(const char *, const char *);
FILE *freopen(const char *, const char *, FILE *);
int fclose(FILE *);
int fflush(FILE *);
size_t fread(void *, size_t, size_t, FILE *);
size_t fwrite(const void *, size_t, size_t, FILE *);
int fseek(FILE *, long, int);
long ftell(FILE *);
int fseeko(FILE *, off_t, int);
off_t ftello(FILE *);
int _fseeki64(FILE *, long long, int);
long long _ftelli64(FILE *);
void rewind(FILE *);
int fgetpos(FILE *, fpos_t *);
int fsetpos(FILE *, const fpos_t *);
int feof(FILE *);
int ferror(FILE *);
void clearerr(FILE *);
int fgetc(FILE *);
int getc(FILE *);
int getchar(void);
int ungetc(int, FILE *);
char *fgets(char *, int, FILE *);
int fputc(int, FILE *);
int putc(int, FILE *);
int putchar(int);
int fputs(const char *, FILE *);
int puts(const char *);
int setvbuf(FILE *, char *, int, size_t);
void setbuf(FILE *, char *);
int remove(const char *);
int rename(const char *, const char *);
void perror(const char *);
int printf(const char *, ...) __attribute__((format(printf, 1, 2)));
int fprintf(FILE *, const char *, ...) __attribute__((format(printf, 2, 3)));
int sprintf(char *, const char *, ...) __attribute__((format(printf, 2, 3)));
int snprintf(char *, size_t, const char *, ...) __attribute__((format(printf, 3, 4)));
int vprintf(const char *, va_list);
int vfprintf(FILE *, const char *, va_list);
int vsprintf(char *, const char *, va_list);
int vsnprintf(char *, size_t, const char *, va_list);
int scanf(const char *, ...);
int fscanf(FILE *, const char *, ...);
int sscanf(const char *, const char *, ...);
int vscanf(const char *, va_list);
int vfscanf(FILE *, const char *, va_list);
int vsscanf(const char *, const char *, va_list);
/* Names of the Windows runtime. */
#define _snprintf snprintf
#define _vsnprintf vsnprintf
int _sscanf_l(const char *, const char *, _locale_t, ...);
int fileno(FILE *);
#define _fileno fileno
FILE *fdopen(int, const char *);
#define _fdopen fdopen
FILE *_wfopen(const wchar_t *, const wchar_t *);
FILE *tmpfile(void);
char *tmpnam(char *);
#ifdef __cplusplus
}
#endif
#endif
