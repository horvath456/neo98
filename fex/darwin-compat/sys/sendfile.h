// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Andreas Horvath
// NEO98 FEX spike: Darwin shim for Linux sendfile(out, in, off, count).
#pragma once
#include <sys/types.h>
#include <unistd.h>
static inline ssize_t fex_linux_sendfile(int out, int in, off_t* off, size_t count) {
  char buf[65536]; size_t done = 0;
  while (done < count) {
    ssize_t n = read(in, buf, sizeof(buf) < count - done ? sizeof(buf) : count - done);
    if (n <= 0) break;
    if (write(out, buf, (size_t)n) != n) return -1;
    done += (size_t)n;
  }
  return (ssize_t)done;
}
#define sendfile fex_linux_sendfile
