// SPDX-License-Identifier: GPL-2.0-or-later
// ctypes bridge for tools/test_kernel_misc_status_original.py.
#include "../src/win98_nt_status.h"
extern "C" unsigned kernel_misc_status(unsigned status) { return Win98NtStatus::ToDosError(status); }
