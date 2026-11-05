/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* The static C runtime of the NEO98 SDK: <fenv.h> over FPCR (rounding) and
   FPSR (the flags of ARM64). Own code. docs/ARM64-APPS.md. */
#include <fenv.h>

static unsigned long long GetControl(void)
{
    unsigned long long value;
    __asm__ volatile("mrs %0, fpcr" : "=r"(value));
    return value;
}
static void SetControl(unsigned long long value) { __asm__ volatile("msr fpcr, %0" : : "r"(value)); }
static unsigned long long GetStatus(void)
{
    unsigned long long value;
    __asm__ volatile("mrs %0, fpsr" : "=r"(value));
    return value;
}
static void SetStatus(unsigned long long value) { __asm__ volatile("msr fpsr, %0" : : "r"(value)); }

const fenv_t __neo98_default_fenv = {0, 0};
int feclearexcept(int mask)
{
    SetStatus(GetStatus() & ~(unsigned long long)(mask & FE_ALL_EXCEPT));
    return 0;
}
int fetestexcept(int mask) { return (int)(GetStatus() & (unsigned)(mask & FE_ALL_EXCEPT)); }
int feraiseexcept(int mask)
{
    SetStatus(GetStatus() | (unsigned)(mask & FE_ALL_EXCEPT));
    return 0;
}
int fegetexceptflag(fexcept_t *flags, int mask)
{
    *flags = (fexcept_t)fetestexcept(mask);
    return 0;
}
int fesetexceptflag(const fexcept_t *flags, int mask)
{
    mask &= FE_ALL_EXCEPT;
    SetStatus((GetStatus() & ~(unsigned long long)mask) | (*flags & (unsigned)mask));
    return 0;
}
int fegetround(void) { return (int)(GetControl() & FE_TOWARDZERO); }
int fesetround(int mode)
{
    if (mode & ~FE_TOWARDZERO) return 1;
    SetControl((GetControl() & ~(unsigned long long)FE_TOWARDZERO) | (unsigned)mode);
    return 0;
}
int fegetenv(fenv_t *environment)
{
    environment->__fpcr = (unsigned)GetControl();
    environment->__fpsr = (unsigned)GetStatus();
    return 0;
}
int fesetenv(const fenv_t *environment)
{
    SetControl(environment->__fpcr);
    SetStatus(environment->__fpsr);
    return 0;
}
int feholdexcept(fenv_t *environment)
{
    fegetenv(environment);
    feclearexcept(FE_ALL_EXCEPT);
    return 0;
}
int feupdateenv(const fenv_t *environment)
{
    const int raised = fetestexcept(FE_ALL_EXCEPT);
    fesetenv(environment);
    return feraiseexcept(raised);
}
