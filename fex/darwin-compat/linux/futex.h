// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Andreas Horvath
// NEO98 FEX spike: Darwin shim for <linux/futex.h>.
// FEXCore calls ::syscall(SYS_futex,...) and ::syscall(SYS_rt_sigprocmask,...).
// Darwin has neither, so this header routes ::syscall through fex_darwin_syscall,
// which maps futex wait/wake onto os_sync_wait_on_address (macOS 14.4+).
// Bitset masks are ignored: waking all waiters is a superset, callers re-check.
#pragma once
#include "fex_darwin_syscall.h"
