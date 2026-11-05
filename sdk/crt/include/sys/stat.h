/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* The static C runtime of the NEO98 SDK (sdk/crt): what a file is, over
   KERNEL32. Sizes and times are 64 bits wide under every name. */
#ifndef NEO98_CRT_SYS_STAT_H
#define NEO98_CRT_SYS_STAT_H
#include <sys/types.h>
#include <time.h>
#ifdef __cplusplus
extern "C" {
#endif
#define _S_IFMT 0xF000
#define _S_IFDIR 0x4000
#define _S_IFCHR 0x2000
#define _S_IFREG 0x8000
#define _S_IREAD 0x0100
#define _S_IWRITE 0x0080
#define _S_IEXEC 0x0040
#define S_IFMT _S_IFMT
#define S_IFDIR _S_IFDIR
#define S_IFCHR _S_IFCHR
#define S_IFREG _S_IFREG
#define S_IREAD _S_IREAD
#define S_IWRITE _S_IWRITE
#define S_IEXEC _S_IEXEC
#define S_IRUSR _S_IREAD
#define S_IWUSR _S_IWRITE
#define S_IXUSR _S_IEXEC
#define S_IRWXU (_S_IREAD | _S_IWRITE | _S_IEXEC)
#define S_ISDIR(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#define S_ISREG(mode) (((mode) & _S_IFMT) == _S_IFREG)
#define S_ISCHR(mode) (((mode) & _S_IFMT) == _S_IFCHR)
struct stat
{
    unsigned st_dev, st_ino;
    unsigned short st_mode;
    short st_nlink, st_uid, st_gid;
    unsigned st_rdev;
    long long st_size;
    time_t st_atime, st_mtime, st_ctime;
};
#define _stat stat
#define _stat64 stat
#define _stati64 stat
#define _fstat fstat
#define _fstat64 fstat
#define _fstati64 fstat
int stat(const char *, struct stat *);
/* By the number of a stream (fileno). */
int fstat(int, struct stat *);
/* The second argument of Unix is accepted and has no meaning here. */
int mkdir(const char *, ...);
#ifdef __cplusplus
}
#endif
#endif
