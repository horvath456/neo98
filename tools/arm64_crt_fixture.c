/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* The static C runtime of the SDK (sdk/crt) in an ARM64 program: memory,
   strings, numbers, sorting, formatting, streams, time, setjmp, math. crt_expected.h, which
   tools/test_arm64_crt_native.py writes, holds what the C library of the host
   answers for the same formats and math functions.
   docs/ARM64-APPS.md. */
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wctype.h>
#include <wchar.h>
#include <locale.h>
#include <malloc.h>
#include <windows.h>
#include <alloca.h>
#include <direct.h>
#include <dirent.h>
#include <io.h>
#include <process.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "crt_expected.h"

static int checks, failures;
static void Check(int id, int condition)
{
    ++checks;
    if (condition) return;
    ++failures;
    printf("FAIL %d\n", id);
}
static int CompareInt(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }
static jmp_buf jump;
static double Deep(int depth, double value)
{
    volatile double keep = value * 2;
    if (!depth) longjmp(jump, 7);
    return Deep(depth - 1, keep) + keep;
}

static void Strings(void)
{
    char text[64];
    Check(100, strlen("hello") == 5 && strnlen("hello", 3) == 3);
    Check(101, !strcmp(strcpy(text, "abc"), "abc") && !strcmp(strcat(text, "def"), "abcdef"));
    Check(102, strncmp("abcd", "abcf", 3) == 0 && strncmp("abcd", "abcf", 4) < 0 && strcmp("b", "a") > 0);
    Check(103, strchr(text, 'd') == text + 3 && strrchr("a/b/c", '/')[1] == 'c' && strchr(text, 'x') == NULL);
    Check(104, strstr("needle in hay", "in") != NULL && strstr("abc", "abd") == NULL);
    Check(105, _stricmp("HeLLo", "hello") == 0 && strncasecmp("ABCx", "abcy", 3) == 0);
    Check(106, strspn("123abc", "0123456789") == 3 && strcspn("abc;def", ";") == 3);
    strcpy(text, "one, two,three");
    Check(107, !strcmp(strtok(text, ", "), "one") && !strcmp(strtok(NULL, ", "), "two") &&
                   !strcmp(strtok(NULL, ", "), "three") && strtok(NULL, ", ") == NULL);
    Check(108, memchr("abcdef", 'd', 6) != NULL && memchr("abcdef", 'z', 6) == NULL);
    Check(109, isdigit('7') && !isdigit('a') && isspace('\t') && toupper('q') == 'Q' && isxdigit('F') && ispunct('!'));
    {
        char *copy = strdup("duplicate");
        Check(110, copy && !strcmp(copy, "duplicate"));
        free(copy);
    }
}
static void Memory(void)
{
    unsigned char *block = (unsigned char *)malloc(1000);
    int *zeroed = (int *)calloc(100, sizeof(int)), sum = 0;
    void *aligned = _aligned_malloc(100, 64);
    Check(200, block != NULL && zeroed != NULL);
    for (int i = 0; i < 1000; ++i) block[i] = (unsigned char)i;
    block = (unsigned char *)realloc(block, 200000);
    Check(201, block != NULL && block[999] == (unsigned char)999 && block[1] == 1);
    for (int i = 0; i < 100; ++i) sum += zeroed[i];
    Check(202, sum == 0);
    Check(203, aligned != NULL && ((uintptr_t)aligned & 63) == 0);
    _aligned_free(aligned);
    free(block);
    free(zeroed);
    free(NULL);
    /* Many blocks, freed in another order. */
    {
        void *blocks[200];
        int ok = 1;
        for (int i = 0; i < 200; ++i) { blocks[i] = malloc((size_t)(i * 37 + 1)); ok &= blocks[i] != NULL; if (ok) memset(blocks[i], i, (size_t)(i * 37 + 1)); }
        for (int i = 0; i < 200; i += 2) free(blocks[i]);
        for (int i = 1; i < 200; i += 2) { ok &= ((unsigned char *)blocks[i])[i * 37] == (unsigned char)i; free(blocks[i]); }
        Check(204, ok);
    }
}
/* --- the allocator (musl's mallocng over VirtualAlloc) ------------------ */
/* Bytes of committed private memory of the process, and the regions that hold
   them: what the allocator has taken from the system and not given back. */
static size_t Committed(unsigned *regions)
{
    MEMORY_BASIC_INFORMATION info;
    size_t bytes = 0;
    unsigned count = 0;
    for (uintptr_t address = 0x10000; address < 0x7fff0000u && VirtualQuery((void *)address, &info, sizeof info) == sizeof info;
         address = (uintptr_t)info.BaseAddress + info.RegionSize)
    {
        if (!info.RegionSize) break;
        if (info.State == MEM_COMMIT) { bytes += info.RegionSize; ++count; }
    }
    if (regions) *regions = count;
    return bytes;
}
static unsigned char Mark(size_t size, size_t index) { return (unsigned char)(size * 31 + index * 7 + 1); }
static void Fill(unsigned char *block, size_t size)
{
    if (!size) return;
    block[0] = Mark(size, 0);
    block[size / 2] = Mark(size, size / 2);
    block[size - 1] = Mark(size, size - 1);
}
static int Filled(const unsigned char *block, size_t size)
{
    return !size || (block[0] == Mark(size, 0) && block[size / 2] == Mark(size, size / 2) && block[size - 1] == Mark(size, size - 1));
}
/* A generator of the test's own: rand has one state for all threads. */
static unsigned Next(unsigned *state) { *state = *state * 1664525u + 1013904223u; return *state >> 8; }

/* Threads against each other: every thread puts its blocks into slots all
   threads share and frees what it finds there, which another thread may have
   allocated. */
#define SLOTS 256
#define THREADS 4
#define ROUNDS 30000
static void *volatile slots[SLOTS];
static volatile long thread_failures, thread_operations;
static DWORD WINAPI Worker(LPVOID parameter)
{
    unsigned state = (unsigned)(uintptr_t)parameter * 2654435761u + 1;
    for (int round = 0; round < ROUNDS; ++round)
    {
        const unsigned draw = Next(&state);
        /* Mostly small, some up to 64 KiB, now and then one of its own mapping. */
        const size_t size = sizeof(size_t) + (draw % 100 < 90 ? draw % 600 : draw % 100 < 99 ? draw % 65000 : 140000 + draw % 300000);
        unsigned char *block = (unsigned char *)(draw & 0x10000 ? calloc(1, size) : malloc(size)), *old;
        if (!block) { __atomic_fetch_add(&thread_failures, 1, __ATOMIC_SEQ_CST); continue; }
        *(size_t *)block = size;
        Fill(block + sizeof(size_t), size - sizeof(size_t));
        old = (unsigned char *)__atomic_exchange_n(&slots[Next(&state) % SLOTS], block, __ATOMIC_SEQ_CST);
        if (old)
        {
            const size_t had = *(size_t *)old;
            if (_msize(old) < had || !Filled(old + sizeof(size_t), had - sizeof(size_t))) __atomic_fetch_add(&thread_failures, 1, __ATOMIC_SEQ_CST);
            if (draw & 0x20000)
            {
                /* realloc of a block another thread may have made. */
                old = (unsigned char *)realloc(old, had + 64);
                if (!old || !Filled(old + sizeof(size_t), had - sizeof(size_t))) __atomic_fetch_add(&thread_failures, 1, __ATOMIC_SEQ_CST);
            }
            free(old);
        }
        __atomic_fetch_add(&thread_operations, 1, __ATOMIC_SEQ_CST);
    }
    return 0;
}
/* musl's lock, which the allocator takes: a thread that finds it held sleeps
   until the holder lets go. */
void __lock(volatile int *);
void __unlock(volatile int *);
static volatile int test_lock[1];
static volatile long lock_order, waiter_started;
static DWORD WINAPI LockWaiter(LPVOID parameter)
{
    (void)parameter;
    __atomic_fetch_add(&waiter_started, 1, __ATOMIC_SEQ_CST);
    __lock(test_lock);
    /* 2: the holder had counted to 1 before it let go. */
    if (lock_order == 1) lock_order = 2;
    __unlock(test_lock);
    return 0;
}
static volatile long counter_plain;
static DWORD WINAPI LockCounter(LPVOID parameter)
{
    (void)parameter;
    for (int i = 0; i < 20000; ++i)
    {
        __lock(test_lock);
        counter_plain = counter_plain + 1;   /* not atomic: the lock makes it so */
        __unlock(test_lock);
    }
    return 0;
}
static void Allocator(void)
{
    /* Sizes across the classes of the allocator and beyond them: every block
       is aligned to 16, holds what was written while all the others exist,
       and knows its room: what was asked for, and not much more. */
    {
        enum { COUNT = 420 };
        static void *blocks[COUNT];
        static size_t sizes[COUNT];
        int ok = 1, aligned = 1, sized = 1, n = 0;
        for (size_t size = 0; size < 300 && n < COUNT; size += 7) sizes[n++] = size;
        for (size_t size = 300; size < 70000 && n < COUNT; size += size / 9 + 13) sizes[n++] = size;
        for (size_t size = 70000; size < 3000000 && n < COUNT; size += size / 3 + 4099) sizes[n++] = size;
        for (int i = 0; i < n; ++i)
        {
            blocks[i] = malloc(sizes[i]);
            ok &= blocks[i] != NULL;
            if (!blocks[i]) continue;
            aligned &= ((uintptr_t)blocks[i] & 15) == 0;
            sized &= _msize(blocks[i]) >= sizes[i] && _msize(blocks[i]) <= sizes[i] + sizes[i] / 4 + 65536 * 2 &&
                     malloc_usable_size(blocks[i]) == _msize(blocks[i]);
            Fill((unsigned char *)blocks[i], sizes[i]);
        }
        Check(210, ok && n > 100);
        Check(211, aligned);
        Check(212, sized);
        ok = 1;
        for (int i = 0; i < n; i += 2) { ok &= blocks[i] && Filled((unsigned char *)blocks[i], sizes[i]); free(blocks[i]); }
        for (int i = 1; i < n; i += 2) { ok &= blocks[i] && Filled((unsigned char *)blocks[i], sizes[i]); free(blocks[i]); }
        Check(213, ok);
        /* malloc(0) answers a block of its own. */
        {
            void *a = malloc(0), *b = malloc(0);
            Check(214, a && b && a != b);
            free(a);
            free(b);
        }
    }
    /* A large block is a mapping of its own: it is the system's again once
       it is freed. */
    {
        MEMORY_BASIC_INFORMATION info;
        const size_t size = 5u << 20;
        unsigned char *block = (unsigned char *)malloc(size);
        Check(215, block != NULL);
        if (block)
        {
            block[0] = 1;
            block[size - 1] = 2;
            Check(216, VirtualQuery(block, &info, sizeof info) == sizeof info && info.State == MEM_COMMIT);
            free(block);
            Check(217, VirtualQuery(block, &info, sizeof info) == sizeof info && info.State == MEM_FREE);
        }
    }
    /* Many small blocks: sample released address ranges with VirtualQuery.
       The old test read through freed pointers because oldmalloc retained
       its heap; a returning allocator must never be read after free. */
    {
        enum { COUNT = 24000 };
        void **blocks = (void **)malloc(COUNT * sizeof(void *));
        unsigned regions_before, regions_full, regions_after, given_up = 0;
        const size_t before = Committed(&regions_before);
        size_t full, after;
        int ok = blocks != NULL;
        for (int i = 0; ok && i < COUNT; ++i)
        {
            blocks[i] = malloc(1000);
            if (!blocks[i]) { ok = 0; break; }
            memset(blocks[i], 0xa5, 1000);
        }
        full = Committed(&regions_full);
        for (int i = 0; ok && i < COUNT; ++i) free(blocks[i]);
        after = Committed(&regions_after);
        for (int i = 0; ok && i < COUNT; i += 100)
        {
            MEMORY_BASIC_INFORMATION info;
            given_up += VirtualQuery(blocks[i], &info, sizeof info) == sizeof info && info.State == MEM_FREE;
        }
        printf("allocator: committed %u KiB in %u regions, with 24 MB of blocks %u KiB in %u, after freeing %u KiB in %u, %u of 240 samples given up\n",
               (unsigned)(before >> 10), regions_before, (unsigned)(full >> 10), regions_full, (unsigned)(after >> 10), regions_after, given_up);
        Check(218, ok && full >= before + (16u << 20));
        Check(219, ok && given_up >= 235);
        /* Release almost all of the bulk commit, including scattered groups
           (the separate heap-release fixture exercises that free order). */
        Check(220, ok && after < before + (1u << 20));
        ok = 1;
        for (int i = 0; i < 2000; ++i)
        {
            unsigned char *zero = (unsigned char *)calloc(10, 100);
            if (!zero) { ok = 0; break; }
            for (int k = 0; k < 1000; ++k) ok &= zero[k] == 0;
            memset(zero, 0xff, 1000);
            blocks[i] = zero;
        }
        for (int i = 0; i < 2000; ++i) free(blocks[i]);
        Check(221, ok);
        free(blocks);
    }
    /* calloc: zeros in a block that held something else, zeros in a large
       one, and no block when the product does not fit. */
    {
        unsigned char *dirty = (unsigned char *)malloc(3000), *zero, *large;
        int ok = dirty != NULL;
        if (dirty) memset(dirty, 0x5a, 3000);
        free(dirty);
        zero = (unsigned char *)calloc(3, 1000);
        ok &= zero != NULL;
        for (int i = 0; zero && i < 3000; ++i) ok &= zero[i] == 0;
        large = (unsigned char *)calloc(1u << 20, 2);
        ok &= large != NULL;
        for (size_t i = 0; large && i < (2u << 20); i += 997) ok &= large[i] == 0;
        Check(222, ok && _msize(zero) >= 3000);
        free(zero);
        free(large);
        errno = 0;
        Check(223, calloc((size_t)-1 / 2, 4) == NULL && errno == ENOMEM);
    }
    /* realloc: the contents move along through small, medium and large
       sizes and back; realloc(NULL, n) allocates; realloc(p, 0) frees and
       answers NULL, as the Windows runtime does. */
    {
        static const size_t steps[] = {10, 11, 100, 5000, 40000, 200000, 3000000, 4000000, 150000, 60000, 900, 20, 1};
        unsigned char *block = (unsigned char *)realloc(NULL, 10);
        size_t have = 10;
        int ok = block != NULL;
        for (size_t i = 0; block && i < have; ++i) block[i] = (unsigned char)(i * 13 + 5);
        for (unsigned step = 1; ok && step < sizeof steps / sizeof steps[0]; ++step)
        {
            const size_t size = steps[step], keep = size < have ? size : have;
            block = (unsigned char *)realloc(block, size);
            if (!block) { ok = 0; break; }
            for (size_t i = 0; i < keep; ++i) ok &= block[i] == (unsigned char)(i * 13 + 5);
            ok &= _msize(block) >= size;
            for (size_t i = keep; i < size; ++i) block[i] = (unsigned char)(i * 13 + 5);
            have = size;
        }
        Check(224, ok);
        Check(225, block && realloc(block, 0) == NULL);
    }
    /* Aligned blocks, by the names of the Windows runtime and by the one of
       C11. */
    {
        int ok = 1;
        for (size_t alignment = 1; alignment <= (1u << 20); alignment *= 2)
        {
            unsigned char *a = (unsigned char *)_aligned_malloc(alignment + 77, alignment);
            unsigned char *b = (unsigned char *)aligned_alloc(alignment, 3 * alignment);
            ok &= a && b && !((uintptr_t)a & (alignment - 1)) && !((uintptr_t)b & (alignment - 1));
            if (a) { memset(a, 1, alignment + 77); ok &= _msize(a) >= alignment + 77; }
            if (b) memset(b, 2, 3 * alignment);
            _aligned_free(a);
            free(b);
        }
        Check(226, ok);
        errno = 0;
        Check(227, _aligned_malloc(100, 24) == NULL && errno == EINVAL);
        _aligned_free(NULL);
    }
    /* No memory: NULL and ENOMEM, and the allocator goes on working. Sizes
       beyond 4 GiB too, which the system below cannot even be asked for. */
    {
        void *blocks[40];
        int count = 0;
        errno = 0;
        Check(228, malloc((size_t)-1) == NULL && errno == ENOMEM && malloc((size_t)-1 / 2) == NULL);
        errno = 0;
        Check(229, malloc(0x100002000ull) == NULL && errno == ENOMEM && malloc(0x3fffff0000ull) == NULL &&
                       realloc(NULL, 0x200000000ull) == NULL && calloc(0x100000, 0x1001) == NULL &&
                       _aligned_malloc(0x100000100ull, 64) == NULL);
        while (count < 40 && (blocks[count] = malloc(64u << 20)) != NULL) ++count;
        Check(230, count >= 1 && count < 40 && errno == ENOMEM);
        {
            void *small = malloc(100);
            Check(231, small != NULL);
            free(small);
        }
        while (count) free(blocks[--count]);
        blocks[0] = malloc(64u << 20);
        Check(232, blocks[0] != NULL);
        free(blocks[0]);
    }
    /* The lock of the allocator: a second thread sleeps while the first holds
       it, and two threads that count under it lose nothing. */
    {
        HANDLE threads[THREADS];
        DWORD id;
        __lock(test_lock);
        threads[0] = CreateThread(NULL, 0, LockWaiter, NULL, 0, &id);
        while (!waiter_started) Sleep(1);
        Sleep(60);
        lock_order = 1;
        __unlock(test_lock);
        Check(233, threads[0] && WaitForSingleObject(threads[0], 20000) == WAIT_OBJECT_0 && lock_order == 2);
        CloseHandle(threads[0]);
        for (int i = 0; i < 2; ++i) threads[i] = CreateThread(NULL, 0, LockCounter, NULL, 0, &id);
        for (int i = 0; i < 2; ++i) { WaitForSingleObject(threads[i], 60000); CloseHandle(threads[i]); }
        Check(234, counter_plain == 40000 && test_lock[0] == 0);
        /* Threads against each other in the allocator itself. */
        for (int i = 0; i < THREADS; ++i) threads[i] = CreateThread(NULL, 0, Worker, (LPVOID)(uintptr_t)(i + 1), 0, &id);
        for (int i = 0; i < THREADS; ++i)
        {
            Check(235 + i, threads[i] && WaitForSingleObject(threads[i], 120000) == WAIT_OBJECT_0);
            CloseHandle(threads[i]);
        }
        for (int i = 0; i < SLOTS; ++i)
        {
            unsigned char *left = (unsigned char *)slots[i];
            if (!left) continue;
            if (!Filled(left + sizeof(size_t), *(size_t *)left - sizeof(size_t))) ++thread_failures;
            free(left);
        }
        Check(239, thread_failures == 0 && thread_operations == THREADS * ROUNDS);
    }
}
static void Numbers(void)
{
    char *end;
    int values[] = {42, -7, 19, 0, 1000, 3, 3, -500};
    int key = 19;
    Check(300, atoi("  -123x") == -123 && atol("77") == 77 && atoll("-9000000000") == -9000000000ll);
    Check(301, strtol("0x1F", &end, 0) == 31 && *end == 0 && strtol("077", NULL, 0) == 63 && strtol("zz", NULL, 36) == 1295);
    Check(302, strtoul("4294967295", NULL, 10) == 4294967295ul && strtoull("18446744073709551615", NULL, 10) == UINT64_MAX);
    errno = 0;
    Check(303, strtol("99999999999", NULL, 10) == 2147483647 && errno == ERANGE);
    Check(304, strtod("1.5", &end) == 1.5 && *end == 0 && strtod("-2.5e3", NULL) == -2500.0 && strtod("0.1", NULL) == 0.1);
    Check(305, strtod("1e22", NULL) == 1e22 && strtod("123456789012345678", NULL) == 123456789012345678.0 &&
                   strtod(".5", NULL) == 0.5 && atof("3.25abc") == 3.25);
    Check(306, isinf(strtod("inf", NULL)) && isnan(strtod("nan", NULL)) && strtod("x", &end) == 0 && *end == 'x');
    Check(307, fabs(strtod("1.7976931348623157e308", NULL) - 1.7976931348623157e308) <= 4e292 &&
                   fabs(strtod("2.2250738585072014e-308", NULL) / 2.2250738585072014e-308 - 1) < 1e-15);
    qsort(values, 8, sizeof values[0], CompareInt);
    Check(308, values[0] == -500 && values[1] == -7 && values[3] == 3 && values[4] == 3 && values[7] == 1000);
    Check(309, bsearch(&key, values, 8, sizeof values[0], CompareInt) == &values[5]);
    key = 20;
    Check(310, bsearch(&key, values, 8, sizeof values[0], CompareInt) == NULL);
    Check(311, abs(-5) == 5 && llabs(-5000000000ll) == 5000000000ll && div(17, 5).quot == 3 && div(17, 5).rem == 2);
    srand(1);
    {
        const int a = rand(), b = rand();
        srand(1);
        Check(312, a == rand() && b == rand() && a >= 0 && a <= RAND_MAX);
    }
    {
        int a = 0, b = 0, n = 0;
        float f = 0;
        char word[16];
        Check(313, sscanf("12 0x1f hello 2.5%n", "%d %i %15s %f%n", &a, &b, word, &f, &n) == 4 && a == 12 && b == 31 &&
                       !strcmp(word, "hello") && f == 2.5f && n == 17);
    }
}
static void Formats(void)
{
    int format_failures = 0;
    char text[400], expected[32];
    Check(400, snprintf(text, sizeof text, "%d %5d|%-5d|%05d %+d %x %X %#x %o %u", 42, 42, 42, 42, 42, 255, 255, 255, 8,
                        4000000000u) > 0 &&
                   !strcmp(text, "42    42|42   |00042 +42 ff FF 0xff 10 4000000000"));
    Check(401, snprintf(text, sizeof text, "%lld %llu %llx %I64d %zu", -9000000000ll, 18446744073709551615ull,
                        0x123456789abcdefull, 77ll, (size_t)5) > 0 &&
                   !strcmp(text, "-9000000000 18446744073709551615 123456789abcdef 77 5"));
    Check(402, snprintf(text, sizeof text, "%s|%10s|%-10s|%.3s|%c|%%", "abc", "abc", "abc", "abcdef", 'z') > 0 &&
                   !strcmp(text, "abc|       abc|abc       |abc|z|%"));
    Check(403, snprintf(text, 5, "%s", "truncated") == 9 && !strcmp(text, "trun"));
    Check(404, snprintf(text, sizeof text, "%.3d %hd %hhu %ld", 7, (short)-3, (unsigned char)200, -5l) > 0 &&
                   !strcmp(text, "007 -3 200 -5"));
    snprintf(expected, sizeof expected, "%p", (void *)(uintptr_t)0x1234abcd);
    Check(405, !strcmp(expected, "000000001234ABCD"));
    Check(406, snprintf(text, sizeof text, "%f %F %e", (double)INFINITY, (double)NAN, -(double)INFINITY) > 0 &&
                   !strcmp(text, "inf NAN -inf"));
    /* Floating point: what the C library of the host makes of the same
       values and formats (crt_expected.h, written by the test). */
    for (unsigned i = 0; i < sizeof format_cases / sizeof format_cases[0]; ++i)
    {
        snprintf(text, sizeof text, format_cases[i].format, format_cases[i].value);
        if (strcmp(text, format_cases[i].expected) && ++format_failures <= 5)
            printf("FORMAT %s: [%s] expected [%s]\n", format_cases[i].format, text, format_cases[i].expected);
    }
    Check(407, format_failures == 0);
}
static void Streams(void)
{
    char path[300], line[64];
    const char *temp = getenv("TEMP");
    FILE *file;
    long size;
    snprintf(path, sizeof path, "%s\\crtfixture.txt", temp ? temp : "C:\\WINDOWS\\TEMP");
    file = fopen(path, "wb");
    Check(500, file != NULL);
    if (!file) return;
    Check(501, fprintf(file, "line %d\n", 1) == 7 && fputs("second line\n", file) >= 0 && fputc('x', file) == 'x');
    for (int i = 0; i < 3000; ++i) fwrite(&i, sizeof i, 1, file);
    Check(502, ftell(file) == 7 + 12 + 1 + 12000 && fclose(file) == 0);
    file = fopen(path, "rb");
    Check(503, file != NULL && fgets(line, sizeof line, file) && !strcmp(line, "line 1\n"));
    Check(504, fgets(line, sizeof line, file) && !strcmp(line, "second line\n") && fgetc(file) == 'x');
    Check(505, ungetc('y', file) == 'y' && fgetc(file) == 'y');
    {
        int value = -1, ok = 1;
        for (int i = 0; i < 3000; ++i) ok &= fread(&value, sizeof value, 1, file) == 1 && value == i;
        Check(506, ok && fread(&value, sizeof value, 1, file) == 0 && feof(file) && !ferror(file));
        Check(507, fseek(file, 20 + 4 * 1234, SEEK_SET) == 0 && fread(&value, 4, 1, file) == 1 && value == 1234);
        Check(508, fseek(file, -8, SEEK_CUR) == 0 && fread(&value, 4, 1, file) == 1 && value == 1233 && ftell(file) == 20 + 4 * 1234);
        Check(509, fseek(file, 0, SEEK_END) == 0 && (size = ftell(file)) == 12020 && fseek(file, -4, SEEK_END) == 0 &&
                       fread(&value, 4, 1, file) == 1 && value == 2999);
    }
    fclose(file);
    file = fopen(path, "a");
    fputs("tail", file);
    fclose(file);
    file = fopen(path, "r+b");
    Check(510, file && fseek(file, 0, SEEK_END) == 0 && ftell(file) == 12024);
    Check(511, fseek(file, 0, SEEK_SET) == 0 && fwrite("LINE", 1, 4, file) == 4 && fseek(file, 0, SEEK_SET) == 0 &&
                   fgets(line, sizeof line, file) && !strcmp(line, "LINE 1\n"));
    fclose(file);
    Check(512, remove(path) == 0 && fopen(path, "rb") == NULL && errno == ENOENT);
    Check(513, fprintf(stderr, "to stderr %d\n", 5) == 12);
}
static void Control(void)
{
    volatile int reached = 0;
    const int code = setjmp(jump);
    if (!code)
    {
        reached = 1;
        Deep(50, 1.0);
        reached = 2;
    }
    Check(600, code == 7 && reached == 1);
    {
        const time_t now = time(NULL);
        time_t fixed = 898777815; /* 1998-06-25 12:30:15 UTC */
        struct tm *parts = gmtime(&fixed), copy;
        Check(601, now > 800000000);
        Check(602, parts->tm_year == 98 && parts->tm_mon == 5 && parts->tm_mday == 25 && parts->tm_hour == 12 &&
                       parts->tm_min == 30 && parts->tm_sec == 15 && parts->tm_wday == 4 && parts->tm_yday == 175);
        copy = *localtime(&fixed);
        Check(603, mktime(&copy) == fixed);
        Check(604, clock() >= 0);
    }
    errno = 0;
    Check(605, errno == 0 && (errno = EINVAL) == EINVAL && !strcmp(strerror(ENOENT), "No such file or directory"));
}
static void Math(void)
{
    typedef double (*Function)(double);
    static const Function functions[] = {MATH_FUNCTIONS};
    int wrong = 0;
    /* The same functions in the C library of the host; the last place may differ. */
    for (unsigned i = 0; i < sizeof math_cases / sizeof math_cases[0]; ++i)
    {
        const double result = functions[math_cases[i].function](math_cases[i].input);
        int64_t a, b;
        memcpy(&a, &result, 8);
        memcpy(&b, &math_cases[i].expected, 8);
        /* Two libraries, each within one unit of the true value. */
        if (result != math_cases[i].expected && (a > b ? a - b : b - a) > 2 && ++wrong <= 5)
            printf("MATH %d(%.17g) = %.17g expected %.17g\n", math_cases[i].function, math_cases[i].input, result, math_cases[i].expected);
    }
    Check(704, wrong == 0);
    Check(700, pow(2.0, 10.0) == 1024.0 && pow(2.0, 0.5) == sqrt(2.0) && fmod(7.5, 2.0) == 1.5 && atan2(1.0, 1.0) > 0.785 &&
                   atan2(1.0, 1.0) < 0.786);
    Check(701, sinf(0.5f) > 0.479f && sinf(0.5f) < 0.480f && sqrtf(16.0f) == 4.0f && powf(3.0f, 2.0f) == 9.0f &&
                   floorf(-1.5f) == -2.0f && fabsf(-2.0f) == 2.0f);
    Check(702, ldexp(0.75, 4) == 12.0 && hypot(3.0, 4.0) == 5.0 && lround(2.5) == 3 && lrint(2.5) == 2 && isnan(sqrt(-1.0)));
    {
        int exponent = 0;
        double whole = 0;
        Check(703, frexp(12.0, &exponent) == 0.75 && exponent == 4 && modf(3.25, &whole) == 0.25 && whole == 3.0 &&
                       copysign(2.0, -1.0) == -2.0 && fmax(1.0, 2.0) == 2.0);
    }
}
/* What came with the C library of musl and the layer below it: exact number
   conversion, wide and multibyte characters, the locale, dates as text,
   formatted input from a stream, temporary files. */
static void Library(void)
{
    char text[128];
    wchar_t wide[32];
    mbstate_t state;
    struct tm moment;
    time_t when = 86400ll * 19000 + 3723; /* 8 January 2022, 01:02:03 UTC */
    FILE *file;
    int a = 0, b = 0;
    double d = 0;
    /* Correctly rounded for any number of digits, and hexadecimal. */
    Check(700, strtod("0.1000000000000000055511151231257827021181583404541015625", NULL) == 0.1 &&
                   strtod("9007199254740993", NULL) == 9007199254740992.0 && strtod("0x1.8p3", NULL) == 12.0 &&
                   strtod("1e400", NULL) == HUGE_VAL && strtof("16777217", NULL) == 16777216.0f);
    Check(701, snprintf(text, sizeof text, "%2$s %1$s", "world", "hello") == 11 && !strcmp(text, "hello world"));
    Check(702, snprintf(text, sizeof text, "%a %ls %lc", 0.5, L"wide", (wint_t)L'!') > 0 && !strcmp(text, "0x1p-1 wide !"));
    Check(703, wcslen(L"hello") == 5 && !wcscmp(wcscpy(wide, L"abc"), L"abc") && wcschr(wide, L'c') == wide + 2 &&
                   wcstol(L"-42z", NULL, 10) == -42 && wcstod(L"2.5", NULL) == 2.5);
    Check(704, swprintf(wide, 32, L"%d-%ls-%s", 7, L"w", "n") == 5 && !wcscmp(wide, L"7-w-n"));
    Check(705, iswalpha(L'a') && iswupper(0x00C4) && towlower(0x00C4) == 0x00E4 && !iswalpha(L'1') && isalpha('z') &&
                   !isalpha(0xE4) && toupper('q') == 'Q');
    /* The "C" locale: a byte is a character; above 127 it has no meaning and
       makes the round trip. */
    memset(&state, 0, sizeof state);
    Check(706, MB_CUR_MAX == 1 && mbrtowc(wide, "A", 1, &state) == 1 && wide[0] == L'A' && mbstowcs(wide, "xyz", 32) == 3 &&
                   wide[2] == L'z' && wcstombs(text, L"wide", sizeof text) == 4 && !strcmp(text, "wide"));
    Check(707, mbrtowc(wide, "\xe4", 1, &state) == 1 && wide[0] == 0xdfe4 && wcrtomb(text, wide[0], &state) == 1 &&
                   (unsigned char)text[0] == 0xe4 && wcrtomb(text, 0x00e4, &state) == (size_t)-1);
    /* UTF-8, when the program asks for it; another locale there is not. */
    Check(708, !setlocale(LC_ALL, "de_DE") && setlocale(LC_ALL, "C.UTF-8") && MB_CUR_MAX == 4 &&
                   mbrtowc(wide, "\xc3\xa4", 2, &state) == 2 && wide[0] == 0x00e4 && wcrtomb(text, 0x20ac, &state) == 3 &&
                   !memcmp(text, "\xe2\x82\xac", 3) && setlocale(LC_ALL, "C") && MB_CUR_MAX == 1);
    Check(709, !strcmp(localeconv()->decimal_point, ".") && !strcmp(setlocale(LC_ALL, NULL), "C"));
    gmtime_r(&when, &moment);
    Check(710, strftime(text, sizeof text, "%Y-%m-%d %H:%M:%S %a %b %j %U %V %G %%", &moment) > 0 &&
                   !strcmp(text, "2022-01-08 01:02:03 Sat Jan 008 01 01 2022 %"));
    Check(711, timegm(&moment) == when && moment.tm_wday == 6 && moment.tm_yday == 7);
    {
        struct tm local = *localtime(&when);
        Check(712, mktime(&local) == when);
    }
    /* Formatted input from a stream, through a temporary file. */
    file = tmpfile();
    Check(713, file != NULL);
    if (file)
    {
        fprintf(file, "12 -7 3.5\nline two\n");
        rewind(file);
        Check(714, fscanf(file, "%d %d %lf", &a, &b, &d) == 3 && a == 12 && b == -7 && d == 3.5);
        Check(715, fgets(text, sizeof text, file) && !strcmp(text, "\n") && fgets(text, sizeof text, file) &&
                       !strcmp(text, "line two\n") && !fgets(text, sizeof text, file) && feof(file));
        fclose(file);
    }
    {
        int numbers[] = {9, 2, 7, 4, 4, 1, 8}, key = 7;
        qsort(numbers, 7, sizeof(int), CompareInt);
        Check(716, numbers[0] == 1 && numbers[6] == 9 && bsearch(&key, numbers, 7, sizeof(int), CompareInt) == &numbers[4]);
    }
    Check(717, sscanf("0x1Fp0 abc[x]", "%lf %3[a-c]", &d, text) == 2 && d == 31.0 && !strcmp(text, "abc"));
}
/* Directories, what a file is, threads and the interlocked operations under
   the names of the Windows runtime and of mingw-w64 (files_windows.c). */
static LONG volatile counted;
static unsigned __stdcall CountEx(void *argument)
{
    for (int i = 0; i < 20000; ++i) InterlockedIncrement(&counted);
    return (unsigned)(uintptr_t)argument + 1;
}
static void CountPlain(void *argument)
{
    for (int i = 0; i < 20000; ++i) InterlockedExchangeAdd(&counted, 2);
    SetEvent((HANDLE)argument);
}
static void Files(void)
{
    char base[300], path[340], other[340], here[300];
    const char *temp = getenv("TEMP");
    struct stat info;
    FILE *file;
    DIR *directory;
    struct dirent *entry;
    int found = 0, dots = 0, count = 0;
    snprintf(base, sizeof base, "%s\\crtdir", temp ? temp : "C:\\WINDOWS\\TEMP");
    snprintf(path, sizeof path, "%s\\one.bin", base);
    snprintf(other, sizeof other, "%s\\sub", base);
    remove(path); _rmdir(other); _rmdir(base);
    Check(800, stat(base, &info) == -1 && errno == ENOENT && _access(base, 0) == -1 && opendir(base) == NULL && errno == ENOENT);
    Check(801, _mkdir(base) == 0 && _mkdir(base) == -1 && errno == EEXIST && mkdir(other, 0755) == 0);
    Check(802, stat(base, &info) == 0 && S_ISDIR(info.st_mode) && !S_ISREG(info.st_mode) && _access(base, 0) == 0);
    file = fopen(path, "wb");
    Check(803, file != NULL);
    if (file)
    {
        for (int i = 0; i < 1000; ++i) fputc(i, file);
        fflush(file);
        Check(804, fstat(fileno(file), &info) == 0 && S_ISREG(info.st_mode) && info.st_size == 1000);
        fclose(file);
    }
    Check(805, stat(path, &info) == 0 && S_ISREG(info.st_mode) && info.st_size == 1000 && (info.st_mode & S_IWRITE));
    /* Written a moment ago, by the clock time reads. */
    Check(806, info.st_mtime > time(NULL) - 120 && info.st_mtime < time(NULL) + 120);
    Check(807, access(path, 2) == 0 && stat("C:\\", &info) == 0 && S_ISDIR(info.st_mode) && fstat(77, &info) == -1 && errno == EBADF);
    directory = opendir(base);
    Check(808, directory != NULL);
    if (directory)
    {
        while ((entry = readdir(directory)) != NULL)
        {
            ++count;
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) ++dots;
            else if (!strcasecmp(entry->d_name, "ONE.BIN") && entry->d_namlen == 7) found |= 1;
            else if (!strcasecmp(entry->d_name, "sub")) found |= 2;
            else found |= 4;
        }
        Check(809, found == 3 && dots == 2 && count == 4);
        rewinddir(directory);
        for (count = 0; readdir(directory); ++count) {}
        Check(810, count == 4 && closedir(directory) == 0);
    }
    Check(811, opendir(path) == NULL && errno == ENOTDIR);
    Check(812, _getcwd(here, sizeof here) == here && _chdir(base) == 0 && stat("one.bin", &info) == 0 && info.st_size == 1000 &&
                   _chdir(here) == 0 && _getcwd(path + 300, 2) == NULL && errno == ERANGE);
    Check(813, _rmdir(base) == -1 && errno == ENOTEMPTY && _unlink(path) == 0 && unlink(path) == -1 && errno == ENOENT &&
                   rmdir(other) == 0 && _rmdir(base) == 0 && stat(base, &info) == -1);
    {
        unsigned id = 0;
        DWORD code = 0;
        const HANDLE done = CreateEventA(NULL, TRUE, FALSE, NULL);
        const uintptr_t thread = _beginthreadex(NULL, 0, CountEx, (void *)(uintptr_t)41, 0, &id);
        const uintptr_t plain = _beginthread(CountPlain, 0, done);
        Check(814, thread != 0 && id != 0 && plain != (uintptr_t)-1);
        Check(815, WaitForSingleObject((HANDLE)thread, 20000) == WAIT_OBJECT_0 && GetExitCodeThread((HANDLE)thread, &code) && code == 42);
        Check(816, WaitForSingleObject(done, 20000) == WAIT_OBJECT_0 && counted == 60000);
        CloseHandle((HANDLE)thread);
        CloseHandle(done);
    }
    {
        LONG volatile value = 5;
        void *volatile pointer = (void *)&value;
        Check(817, InterlockedIncrement(&value) == 6 && InterlockedDecrement(&value) == 5 && InterlockedExchange(&value, 9) == 5 &&
                       InterlockedCompareExchange(&value, 1, 8) == 9 && value == 9 && InterlockedCompareExchange(&value, 1, 9) == 9 &&
                       value == 1 && InterlockedExchangeAdd(&value, 4) == 1 && value == 5);
        Check(818, InterlockedExchangePointer(&pointer, here) == (void *)&value &&
                       InterlockedCompareExchangePointer(&pointer, path, here) == (void *)here && pointer == (void *)path);
    }
    {
        /* More than a page, so the frame is probed. */
        unsigned char *room = alloca(70000), *more = _alloca(16);
        memset(room, 0x5A, 70000);
        memset(more, 0xA5, 16);
        Check(819, room[0] == 0x5A && room[69999] == 0x5A && more[15] == 0xA5 && _tolower('Q') == 'q' && _toupper('q') == 'Q' &&
                       _getpid() == (int)GetCurrentProcessId());
    }
}

static void AtExit(void) { printf("atexit ran\n"); }

int main(int argc, char **argv)
{
    (void)argv;
    atexit(AtExit);
    Strings();
    Memory();
    Allocator();
    Numbers();
    Formats();
    Streams();
    Control();
    Math();
    Library();
    Files();
    assert(argc >= 1);
    printf("CRT checks=%d failures=%d\n", checks, failures);
    return failures;
}
