// SPDX-License-Identifier: GPL-2.0-or-later
// C-library boundary. Circle owns allocations; no second heap or sbrk.
#include <reent.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>
#include "compat.h"
extern unsigned long long neo98_clock_us(void);
extern void neo98_diagnostic(const char *message);
void *_malloc_r(struct _reent *r, size_t size) { (void)r; return malloc(size); }
void *_calloc_r(struct _reent *r, size_t n, size_t size) { (void)r; return calloc(n, size); }
void *_realloc_r(struct _reent *r, void *p, size_t size) { (void)r; return realloc(p, size); }
void _free_r(struct _reent *r, void *p) { (void)r; free(p); }
int getpagesize(void) { return 65536; }
long sysconf(int name)
{
    unsigned long ctr;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    if (name == _SC_LEVEL1_ICACHE_LINESIZE) return 4ul << (ctr & 15);
    if (name == _SC_LEVEL1_DCACHE_LINESIZE) return 4ul << ((ctr >> 16) & 15);
    if (name == _SC_PAGESIZE) return 65536;
    errno = EINVAL; return -1;
}
void _fini(void) {} // Circle owns kernel shutdown and C++ destruction.
int clock_gettime(clockid_t id, struct timespec *value)
{
    if (id != CLOCK_MONOTONIC || !value) { errno = EINVAL; return -1; }
    unsigned long long us = neo98_clock_us();
    value->tv_sec = us / 1000000; value->tv_nsec = us % 1000000 * 1000;
    return 0;
}
int _gettimeofday(struct timeval *value, void *zone)
{
    // Kernel uptime, used only to seed Unicorn's non-cryptographic PRNG.
    (void)zone;
    unsigned long long us = neo98_clock_us();
    value->tv_sec = us / 1000000; value->tv_usec = us % 1000000; return 0;
}
int _getpid(void) { return 1; }
pid_t getppid(void) { return 0; }
int _open(const char *path, int flags, ...) { (void)path; (void)flags; errno = ENOENT; return -1; }
int _close(int fd) { (void)fd; errno = EBADF; return -1; }
int _read(int fd, void *data, size_t size) { (void)fd; (void)data; (void)size; errno = EBADF; return -1; }
off_t _lseek(int fd, off_t offset, int origin) { (void)fd; (void)offset; (void)origin; errno = EBADF; return -1; }
int _fstat(int fd, struct stat *status)
{
    if (fd != 1 && fd != 2) { errno = EBADF; return -1; }
    status->st_mode = S_IFCHR; return 0;
}
int _isatty(int fd) { return fd == 1 || fd == 2; }
int _write(int fd, const void *data, size_t size)
{
    if (fd != 1 && fd != 2) { errno = EBADF; return -1; }
    const char *source = data;
    size_t done = 0;
    while (done < size) {
        char text[256]; size_t n = size - done; if (n > 255) n = 255;
        for (size_t i = 0; i < n; ++i) text[i] = source[done+i];
        text[n] = 0; neo98_diagnostic(text); done += n;
    }
    return size;
}
int _kill(int pid, int signal) { (void)pid; (void)signal; errno = ENOSYS; return -1; }
void _exit(int status) { (void)status; neo98_diagnostic("C library requested process termination"); for (;;) __asm__ volatile("wfe"); }
