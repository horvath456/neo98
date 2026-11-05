// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_crt.h"
#include "../src/win98_kernel.h"
extern "C" void control(unsigned word, unsigned requested, unsigned mask, unsigned control87, unsigned *out)
{
    const auto result = Win98Crt::Control(word, requested, mask, control87);
    out[0] = result.value;
    out[1] = result.word;
}
extern "C" void error_mode(unsigned previous, unsigned requested, unsigned mutate, unsigned *out)
{
    unsigned short saved = previous;
    out[0] = Win98::SetErrorMode(saved, requested,
                                 [&](unsigned word)
                                 {
                                     out[2] = word;
                                     out[3] = saved;
                                     if (mutate)
                                         saved = mutate;
                                 });
    out[1] = saved;
}
