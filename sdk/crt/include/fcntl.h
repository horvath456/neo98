/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* Windows file descriptor flags, used by the libc++ Windows adapter. */
#ifndef NEO98_CRT_FCNTL_H
#define NEO98_CRT_FCNTL_H
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_APPEND 8
#define O_CREAT 0x100
#define O_TRUNC 0x200
#define O_EXCL 0x400
#define O_BINARY 0x8000
#define O_TEXT 0x4000
#define O_NONBLOCK 0
#endif
