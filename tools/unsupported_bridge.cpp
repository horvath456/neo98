// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_kernel.h"
extern "C" unsigned unsupported(unsigned encoding) { return Win98::UnsupportedResult(encoding); }
