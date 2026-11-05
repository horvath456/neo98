/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* Start code of an ARM64 application: the entry of a console program, without a C
   runtime. Everything here is linked statically into the program, so the same
   file runs wherever KERNEL32 exists. docs/ARM64-APPS.md. */
#include <windows.h>

int main(int argc, char **argv);
void __neo98_run_initializers(void);
void __neo98_run_exit_functions(void);

void __neo98_arguments(void);
/* sdk/crt/src/signal.c: what the runtime does with an exception no frame took. */
LONG WINAPI __neo98_unhandled_filter(struct _EXCEPTION_POINTERS *pointers);
extern int __argc;
extern char **__argv;
void mainCRTStartup(void)
{
    __neo98_arguments();
    SetUnhandledExceptionFilter(__neo98_unhandled_filter);
    __neo98_run_initializers();
    const int result = main(__argc, __argv);
    __neo98_run_exit_functions();
    ExitProcess((UINT)result);
}
