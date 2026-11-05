// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Andreas Horvath
// NEO98 FEX spike: Darwin shim for <sys/prctl.h>; every prctl fails with ENOSYS.
#pragma once
#include <errno.h>
static inline int prctl(int, ...) { errno = ENOSYS; return -1; }
