/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* The static C runtime of the NEO98 SDK: what <stdlib.h>, <errno.h> and
   <assert.h> take from the system: errno per thread, the end of the program,
   the environment. Numbers, sorting, searching and the allocator are musl's
   (memory: malloc_windows.c, mman_windows.c). Own code over KERNEL32.
   docs/ARM64-APPS.md. */
#include <windows.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "libc.h"
#include "pthread_impl.h"

void __neo98_run_exit_functions(void);

/* --- errno: one per thread ---------------------------------------------- */
static DWORD errno_slot = 0xffffffffu;
static int errno_fallback;
int *_errno(void)
{
    if (errno_slot == 0xffffffffu) errno_slot = TlsAlloc();
    if (errno_slot == 0xffffffffu) return &errno_fallback;
    int *value = (int *)TlsGetValue(errno_slot);
    if (!value)
    {
        value = (int *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(int));
        if (!value) return &errno_fallback;
        TlsSetValue(errno_slot, value);
    }
    return value;
}

/* --- the end of the program ---------------------------------------------- */
void _exit(int code)
{
    ExitProcess((UINT)code);
    for (;;) {}
}
/* The streams of musl flush themselves at the end, if the program has any:
   musl's __stdio_exit, which is linked with the first stream function. */
void __stdio_exit(void);
static void NoStreams(void) {}
extern __typeof(NoStreams) __stdio_exit __attribute__((__weak__, __alias__("NoStreams")));
void exit(int code)
{
    __neo98_run_exit_functions();
    __stdio_exit();
    _exit(code);
}
void _Exit(int code) { _exit(code); }
/* quick_exit: the functions registered for it, and no others. */
static void (*quick_functions[32])(void);
static int quick_count;
int at_quick_exit(void (*function)(void))
{
    if (quick_count == 32) return 1;
    quick_functions[quick_count++] = function;
    return 0;
}
void quick_exit(int code)
{
    while (quick_count) quick_functions[--quick_count]();
    _exit(code);
}

/* What the sources of musl read of its process and thread records
   (musl-shim/pthread_impl.h): one locale for the process. Stream locks and
   internal locks of musl (the allocator and list of open streams) are
   always enabled: a program makes
   its threads with CreateThread, and the runtime does not learn of them.
   mallocng needs actual 4 KiB pages for its metadata and guard page.
   VirtualAlloc rounds reservations to the separate 64 KiB granularity. */
static size_t windows_auxv[] = {0};
struct __libc __libc = {.threaded = 1, .need_locks = 1, .page_size = 4096, .auxv = windows_auxv};
unsigned long __neo98_stdio_thread_id(void) { return GetCurrentThreadId(); }
/* abort raises SIGABRT first: a handler of the program may see it. Then the
   process ends with the code the Windows runtime uses. */
int __neo98_raise_abort(void);
void abort(void)
{
    static const char text[] = "abnormal program termination\r\n";
    DWORD written;
    __neo98_raise_abort();
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), text, sizeof text - 1, &written, NULL);
    _exit(3);
}
void _assert(const char *expression, const char *file, unsigned line)
{
    char text[512];
    DWORD written;
    wsprintfA(text, "Assertion failed: %.200s, file %.200s, line %u\r\n", expression, file, line);
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), text, (DWORD)lstrlenA(text), &written, NULL);
    abort();
}
char *getenv(const char *name)
{
    /* One buffer per name would leak; the standard allows the next call to
       overwrite the answer. */
    static char value[1024];
    const DWORD n = GetEnvironmentVariableA(name, value, sizeof value);
    return n && n < sizeof value ? value : NULL;
}
int system(const char *command)
{
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    DWORD code = 0;
    char line[1024];
    if (!command) return 0;
    if (strlen(command) >= sizeof line) return -1;
    strcpy(line, command);
    memset(&startup, 0, sizeof startup);
    startup.cb = sizeof startup;
    if (!CreateProcessA(NULL, line, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process)) return -1;
    WaitForSingleObject(process.hProcess, INFINITE);
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return (int)code;
}


/* The generator of the Windows runtime, with its range (RAND_MAX is 7FFFh):
   a program for Windows may count on both. */
static unsigned random_state = 1;
void srand(unsigned seed) { random_state = seed; }
int rand(void)
{
    random_state = random_state * 214013u + 2531011u;
    return (int)((random_state >> 16) & 0x7fff);
}

/* A random number that is not a sequence of the program: the clocks of the
   system stirred together. Not for cryptography. */
int rand_s(unsigned int *out)
{
    static unsigned long long state;
    LARGE_INTEGER counter;
    FILETIME now;
    if (!out) return EINVAL;
    QueryPerformanceCounter(&counter);
    GetSystemTimeAsFileTime(&now);
    state ^= (unsigned long long)counter.QuadPart * 0x9e3779b97f4a7c15ull;
    state ^= ((unsigned long long)now.dwHighDateTime << 32 | now.dwLowDateTime) + GetCurrentThreadId() + GetTickCount();
    state ^= state >> 33;
    state *= 0xff51afd7ed558ccdull;
    state ^= state >> 33;
    state *= 0xc4ceb9fe1a85ec53ull;
    state ^= state >> 33;
    *out = (unsigned int)(state >> 16);
    return 0;
}
