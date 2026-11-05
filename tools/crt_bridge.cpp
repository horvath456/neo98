// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_crt.h"
extern "C" void original_crt_prolog(unsigned sp, unsigned ret, unsigned ax, unsigned bp, unsigned previous,
                                    Win98Crt::Prolog *out)
{
    *out = Win98Crt::ExceptionProlog(sp, ret, ax, bp, previous);
}
