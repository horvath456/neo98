/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* The static C runtime of the NEO98 SDK (sdk/crt): the entries of a
   directory as mingw-w64 offers them, over FindFirstFileA. */
#ifndef NEO98_CRT_DIRENT_H
#define NEO98_CRT_DIRENT_H
#ifdef __cplusplus
extern "C" {
#endif
struct dirent
{
    long d_ino;
    unsigned short d_reclen;
    unsigned short d_namlen;
    char d_name[260];
};
typedef struct __neo98_dir DIR;
/* A directory to read, or a null pointer with errno set. */
DIR *opendir(const char *);
/* The next entry (valid until the next call), or a null pointer at the end. */
struct dirent *readdir(DIR *);
int closedir(DIR *);
void rewinddir(DIR *);
#ifdef __cplusplus
}
#endif
#endif
