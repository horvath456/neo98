/* SPDX-License-Identifier: GPL-2.0-or-later
 * Minimal startup using Win98's MSVCRT, without the modern MinGW CRT startup.
 * Wine's tests and test expectations remain upstream code. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int main(int, char **);
extern int __cdecl __getmainargs(int *, char ***, char ***, int, int *);

/* No C++ constructors in these C test programs. GCC emits this call in main. */
void __main(void) {}

/* Current MinGW's assert shim requires a newer CRT. Win98's assertion failure
 * remains fatal; keep its message in the process log. */
void _assert(const char *expression, const char *file, unsigned line)
{
    fprintf(stdout, "C_ASSERT %s:%u: %s\n", file, line, expression);
    fflush(stdout);
    ExitProcess(249);
}

void mainCRTStartup(void)
{
    int argc, mode = 0, result;
    char **argv, **environment, log[MAX_PATH], suffix[32];
    __getmainargs(&argc, &argv, &environment, 0, &mode);
    if (!GetModuleFileNameA(NULL, log, sizeof(log)) || strlen(log) > MAX_PATH - 40)
        ExitProcess(250);
    /* Children of process/thread tests must never truncate their parent's log. */
    sprintf(suffix, ".%lu.LOG", GetCurrentProcessId());
    strcat(log, suffix);
    if (!freopen(log, "w", stdout)) ExitProcess(250);
    setvbuf(stdout, NULL, _IOFBF, 4096);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    printf("NEO98_WINE_BEGIN pid=%lu\n", GetCurrentProcessId());
    result = main(argc, argv);
    printf("NEO98_WINE_END pid=%lu exit=%d\n", GetCurrentProcessId(), result);
    fflush(stdout);
    ExitProcess(result);
}
