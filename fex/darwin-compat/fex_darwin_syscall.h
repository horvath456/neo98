// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Andreas Horvath
// NEO98 FEX spike: minimal Linux syscall emulation for FEXCore on Darwin.
#pragma once
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <os/os_sync_wait_on_address.h>

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_WAIT_BITSET 9
#define FUTEX_WAKE_BITSET 10
#define FUTEX_PRIVATE_FLAG 128
#define FUTEX_CLOCK_REALTIME 256

#define SYS_futex 0x7F000001
#define SYS_rt_sigprocmask 0x7F000002
#define SYS_gettid 0x7F000003
#define SYS_tgkill 0x7F000004
#define SYS_getcpu 0x7F000005
#define SYS_renameat2 0x7F000006
#define SYS_getrandom 0x7F000007

static inline long fex_darwin_syscall(long nr, ...) {
  va_list ap;
  va_start(ap, nr);
  long result = -1;
  if (nr == SYS_futex) {
    uint32_t* addr = va_arg(ap, uint32_t*);
    int op = va_arg(ap, int) & ~(FUTEX_PRIVATE_FLAG | FUTEX_CLOCK_REALTIME);
    uint32_t val = va_arg(ap, uint32_t);
    const struct timespec* ts = va_arg(ap, const struct timespec*);
    if (op == FUTEX_WAIT || op == FUTEX_WAIT_BITSET) {
      int r;
      if (ts && op == FUTEX_WAIT) {
        uint64_t ns = (uint64_t)ts->tv_sec * 1000000000ULL + (uint64_t)ts->tv_nsec;
        r = os_sync_wait_on_address_with_timeout(addr, val, 4, OS_SYNC_WAIT_ON_ADDRESS_NONE, OS_CLOCK_MACH_ABSOLUTE_TIME, ns);
      } else {
        r = os_sync_wait_on_address(addr, val, 4, OS_SYNC_WAIT_ON_ADDRESS_NONE);
      }
      result = r < 0 ? -1 : 0;
    } else if (op == FUTEX_WAKE || op == FUTEX_WAKE_BITSET) {
      int count = (int)val;
      int r = count == 1 ? os_sync_wake_by_address_any(addr, 4, OS_SYNC_WAKE_BY_ADDRESS_NONE)
                         : os_sync_wake_by_address_all(addr, 4, OS_SYNC_WAKE_BY_ADDRESS_NONE);
      if (r < 0 && errno == ENOENT) { errno = 0; r = 0; }
      result = r < 0 ? -1 : 0;
    } else {
      errno = ENOSYS;
    }
  } else if (nr == SYS_rt_sigprocmask) {
    int how = va_arg(ap, int);
    const uint64_t* set = va_arg(ap, const uint64_t*);
    uint64_t* old = va_arg(ap, uint64_t*);
    sigset_t s = set ? (sigset_t)*set : 0, o = 0;
    result = pthread_sigmask(how, set ? &s : NULL, &o);
    if (old) *old = o;
  } else if (nr == SYS_gettid) {
    uint64_t tid = 0;
    pthread_threadid_np(NULL, &tid);
    result = (long)tid;
  } else {
    errno = ENOSYS;
  }
  va_end(ap);
  return result;
}
#define syscall fex_darwin_syscall
