// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Use newlib's headers before Clang's freestanding stdint.h so its inttypes
// macros and integer typedefs are configured together.
#include <stdint.h>
#include <setjmp.h>
#include <time.h>
typedef jmp_buf sigjmp_buf;
// Circle has no process signals to save/restore.
#define sigsetjmp(environment, save_signals) setjmp(environment)
#define siglongjmp(environment, value) longjmp(environment, value)
#define CLOCK_MONOTONIC 1
int clock_gettime(clockid_t id, struct timespec *value);
int nanosleep(const struct timespec *request, struct timespec *remaining);
