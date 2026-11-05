/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* NEO98: musl's src/internal/atomic.h, as far as the imported sources use
   it, over the builtins of the compiler. musl's own header takes a_ctz_l for
   the trailing zeros of a size_t (qsort.c); long has 32 bits under Windows,
   so here it counts in 64. Not part of musl. */
#ifndef NEO98_MUSL_ATOMIC_H
#define NEO98_MUSL_ATOMIC_H
#include <stdint.h>
static inline int a_cas(volatile int *p, int t, int s)
{
    __atomic_compare_exchange_n(p, &t, s, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return t;
}
static inline int a_swap(volatile int *p, int v) { return __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST); }
static inline int a_fetch_add(volatile int *p, int v) { return __atomic_fetch_add(p, v, __ATOMIC_SEQ_CST); }
static inline void a_inc(volatile int *p) { __atomic_fetch_add(p, 1, __ATOMIC_SEQ_CST); }
static inline void a_dec(volatile int *p) { __atomic_fetch_sub(p, 1, __ATOMIC_SEQ_CST); }
static inline void a_or(volatile int *p, int v) { __atomic_fetch_or(p, v, __ATOMIC_SEQ_CST); }
static inline void a_and(volatile int *p, int v) { __atomic_fetch_and(p, v, __ATOMIC_SEQ_CST); }
static inline void a_or_64(volatile uint64_t *p, uint64_t v) { __atomic_fetch_or(p, v, __ATOMIC_SEQ_CST); }
static inline void a_and_64(volatile uint64_t *p, uint64_t v) { __atomic_fetch_and(p, v, __ATOMIC_SEQ_CST); }
static inline void a_store(volatile int *p, int v) { __atomic_store_n(p, v, __ATOMIC_SEQ_CST); }
static inline void a_barrier(void) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
static inline void a_spin(void) { __asm__ __volatile__("yield"); }
static inline _Noreturn void a_crash(void) { __builtin_trap(); }
static inline int a_ctz_32(uint32_t x) { return __builtin_ctz(x); }
static inline int a_ctz_64(uint64_t x) { return __builtin_ctzll(x); }
static inline int a_clz_32(uint32_t x) { return __builtin_clz(x); }
static inline int a_clz_64(uint64_t x) { return __builtin_clzll(x); }
static inline int a_ctz_l(uint64_t x) { return __builtin_ctzll(x); }
#endif
