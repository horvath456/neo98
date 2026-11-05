// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_queue_status.h"

extern "C" unsigned queue_status_mask(unsigned flags) {
    return Win98QueueStatus::Mask(flags);
}

extern "C" unsigned queue_status_run(unsigned current, unsigned *changed,
                                      unsigned flags, unsigned input) {
    const unsigned mask = input ? Win98QueueStatus::InputMask
                                 : Win98QueueStatus::Mask(flags);
    return Win98QueueStatus::Query(current, *changed, mask, input != 0);
}
