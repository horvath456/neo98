/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* The static C runtime of the NEO98 SDK (sdk/crt): x19..x30, sp, d8..d15.
   longjmp restores registers and does not unwind. */
#ifndef NEO98_CRT_SETJMP_H
#define NEO98_CRT_SETJMP_H
#ifdef __cplusplus
extern "C" {
#endif
typedef unsigned long long jmp_buf[24];
int setjmp(jmp_buf) __attribute__((returns_twice));
_Noreturn void longjmp(jmp_buf, int);
#define _setjmp setjmp
#ifdef __cplusplus
}
#endif
#endif
