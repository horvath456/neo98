// SPDX-License-Identifier: GPL-2.0-or-later
// C++ tests use the compiler's real CRT startup, constructors and unwinder.
// This object adds process logs without replacing the test's main function.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <io.h>

struct ProcessLog {
    ProcessLog() {
        char path[MAX_PATH], suffix[40];
        if (!GetModuleFileNameA(NULL, path, sizeof(path)) || strlen(path) > MAX_PATH - 40)
            ExitProcess(250);
        sprintf(suffix, ".%lu.LOG", GetCurrentProcessId());
        strcat(path, suffix);
        if (!freopen(path, "w", stdout)) ExitProcess(250);
        _dup2(_fileno(stdout), _fileno(stderr));
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
        printf("NEO98_WINE_BEGIN pid=%lu cpp_crt\n", GetCurrentProcessId());
        fflush(stdout);
    }
    ~ProcessLog() {
        printf("NEO98_WINE_END pid=%lu cpp_crt\n", GetCurrentProcessId());
        fflush(stdout);
    }
};
static ProcessLog process_log __attribute__((init_priority(101)));
