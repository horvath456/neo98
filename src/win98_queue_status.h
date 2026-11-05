// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98QueueStatus {
// USER.EXE. Queue collection/input synchronization are providers.
constexpr unsigned Mask(unsigned flags) { return flags & 0x417f; }
constexpr unsigned InputMask = 0x17f;
inline unsigned Query(unsigned current, unsigned &changed, unsigned mask, bool input) {
    const unsigned result = changed & mask;
    changed &= ~mask;
    return input ? result & 5 : ((current & mask) << 16) | result;
}
}
