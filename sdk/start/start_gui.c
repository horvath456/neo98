/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* Start code of an ARM64 application: the entry of a program of the Windows subsystem, without a C
   runtime. Everything here is linked statically into the program, so the same
   file runs wherever KERNEL32 exists. docs/ARM64-APPS.md. */
#include <windows.h>

/* The command line after the program name, the way WinMain receives it. */
static char *SkipProgramName(char *p)
{
    if (*p == '"')
    {
        ++p;
        while (*p && *p != '"') ++p;
        if (*p == '"') ++p;
    }
    else
        while (*p && *p != ' ' && *p != '\t') ++p;
    while (*p == ' ' || *p == '\t') ++p;
    return p;
}

void __neo98_run_initializers(void);
void __neo98_run_exit_functions(void);

void __neo98_arguments(void);
/* sdk/crt/src/signal.c: what the runtime does with an exception no frame took. */
LONG WINAPI __neo98_unhandled_filter(struct _EXCEPTION_POINTERS *pointers);
void WinMainCRTStartup(void)
{
    __neo98_arguments();
    SetUnhandledExceptionFilter(__neo98_unhandled_filter);
    __neo98_run_initializers();
    const int result = WinMain(GetModuleHandleA(NULL), NULL, SkipProgramName(GetCommandLineA()), SW_SHOWDEFAULT);
    __neo98_run_exit_functions();
    ExitProcess((UINT)result);
}
