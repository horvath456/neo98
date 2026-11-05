// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Andreas Horvath
// NEO98 FEX spike: force-included on Darwin (-include). Glibc names FEXCore expects.
#pragma once
#include <stdlib.h>
#include <sys/mman.h>
#include <malloc/malloc.h>
#ifndef MADV_HUGEPAGE
#define MADV_HUGEPAGE 14   /* Linux value; Darwin madvise rejects it with EINVAL, harmless */
#endif
#ifndef MADV_NOHUGEPAGE
#define MADV_NOHUGEPAGE 15
#endif
#ifndef MADV_DONTDUMP
#define MADV_DONTDUMP 16
#endif
#ifdef __cplusplus
extern "C++" {
inline void* memalign(size_t align, size_t size) {
  void* p = nullptr;
  return posix_memalign(&p, align < sizeof(void*) ? sizeof(void*) : align, size) == 0 ? p : nullptr;
}
inline size_t malloc_usable_size(void* p) { return malloc_size(p); }
}
#endif
