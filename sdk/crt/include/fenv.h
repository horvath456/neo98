/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* The static C runtime of the NEO98 SDK (sdk/crt): the floating-point
   environment of ARM64, FPCR and FPSR. */
#ifndef NEO98_CRT_FENV_H
#define NEO98_CRT_FENV_H
#ifdef __cplusplus
extern "C" {
#endif
#define FE_INVALID 1
#define FE_DIVBYZERO 2
#define FE_OVERFLOW 4
#define FE_UNDERFLOW 8
#define FE_INEXACT 16
#define FE_ALL_EXCEPT 31
#define FE_TONEAREST 0
#define FE_UPWARD 0x400000
#define FE_DOWNWARD 0x800000
#define FE_TOWARDZERO 0xc00000
typedef unsigned int fexcept_t;
typedef struct { unsigned int __fpcr, __fpsr; } fenv_t;
extern const fenv_t __neo98_default_fenv;
#define FE_DFL_ENV (&__neo98_default_fenv)
int feclearexcept(int);
int fegetexceptflag(fexcept_t *, int);
int feraiseexcept(int);
int fesetexceptflag(const fexcept_t *, int);
int fetestexcept(int);
int fegetround(void);
int fesetround(int);
int fegetenv(fenv_t *);
int feholdexcept(fenv_t *);
int fesetenv(const fenv_t *);
int feupdateenv(const fenv_t *);
#ifdef __cplusplus
}
#endif
#endif
